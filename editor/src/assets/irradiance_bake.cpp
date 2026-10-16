#include "assets/irradiance_bake.h"

#include "assets/editor_assets.h"
#include "asset/artifact/environment_artifact.h"
#include "asset/artifact/mesh_artifact.h"
#include "asset/artifact/model_artifact.h"
#include "asset/artifact/texture_artifact.h"
#include "asset/default_assets.h"
#include "asset/import/import_service.h"
#include "asset/import/input_snapshot.h"
#include "asset/import/mesh_importer.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "common/binary.h"
#include "core/task_scheduler.h"
#include "render/material/material_layout.h"
#include "scene/command_history.h"
#include "scene/scene.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>

namespace CometEditor {
    using namespace Comet;
    namespace GI = Comet::IrradianceBake;
    namespace {
        struct WorkProgress {
            std::atomic<bool> cancelled{false};
            std::atomic<uint32_t> completed{0};
            std::atomic<uint32_t> total{0};
        };
        struct WorkOutput {
            IrradianceData data;
            IrradianceBake::Report report;
        };

        template<typename T> T checked(Result<T> result) {
            if(!result)
                throw std::runtime_error(result.error());
            return std::move(result).value();
        }

        // 读取已有产物，不在 Worker 改写数据库、发布资产或访问 GPU。
        class SourceReader {
        public:
            SourceReader(ProjectPaths paths, AssetImportLimits limits,
                std::map<AssetHandle, AssetRecord> records, const WorkProgress& progress)
                : m_paths(std::move(paths)), m_limits(limits), m_imports(m_paths, limits),
                  m_records(std::move(records)), m_progress(progress) {}

            GI::Input read(const RenderScene& scene) {
                GI::Input input;
                std::map<AssetHandle, uint32_t> materials;
                for(const auto& item : scene.render_items) {
                    check_cancelled();
                    if(!materials.contains(item.material_handle)) {
                        materials.emplace(
                            item.material_handle, static_cast<uint32_t>(input.materials.size()));
                        input.materials.push_back(material(item.material_handle));
                    }
                    input.objects.push_back({mesh(item.mesh_handle), item.model_matrix,
                        materials.at(item.material_handle)});
                }
                input.lights = scene.lights;
                if(scene.environment.lighting && scene.environment.asset) {
                    const auto& record = asset(scene.environment.asset, AssetType::Environment);
                    auto artifact = EnvironmentArtifact::load(
                        m_imports.environment_artifact_path(record.handle), record.handle,
                        remaining(m_limits.texture_working_bytes));
                    if(!artifact)
                        fail(record, "环境 CPU 产物未就绪，请等待导入完成");
                    check_source(record, artifact->source);
                    auto data = std::make_shared<TextureData>(std::move(artifact->data.background));
                    reserve(data->pixels.size());
                    input.environment = std::move(data);
                    input.environment_intensity = scene.environment.lighting_intensity;
                    input.environment_rotation = scene.environment.rotation;
                }
                check_cancelled();
                return input;
            }
            std::size_t input_bytes() const { return m_bytes; }

        private:
            [[noreturn]] static void fail(const AssetRecord& record, const std::string& message) {
                throw std::runtime_error(record.path.generic_string() + ": " + message);
            }
            void check_cancelled() const {
                if(m_progress.cancelled.load(std::memory_order_relaxed))
                    throw std::runtime_error("GI 烘焙已取消");
            }
            std::size_t remaining(const std::size_t limit) const {
                return std::min(limit, m_limits.async.working_bytes - m_bytes);
            }
            void reserve(const std::size_t bytes) {
                if(bytes > remaining(m_limits.async.working_bytes))
                    throw std::runtime_error("GI 输入超过 CPU 内存预算");
                m_bytes += bytes;
            }
            const AssetRecord& asset(const AssetHandle handle, const AssetType type) const {
                const auto found = m_records.find(handle);
                if(found == m_records.end() || found->second.type != type)
                    throw std::runtime_error(
                        "GI 输入资产缺失或类型错误：" + std::to_string(handle.value()));
                return found->second;
            }
            void check_source(
                const AssetRecord& record, const ImportInputFingerprint& fingerprint) const {
                auto inputs = checked(
                    capture_import_inputs(m_paths.assets(), m_paths.assets() / record.path, {}));
                if(inputs.files.front() != fingerprint)
                    fail(record, "CPU 产物已过期，请等待重新导入");
            }
            const ModelArtifact& model(const AssetRecord& child) {
                const auto owner = child.source_asset;
                if(auto found = m_models.find(owner); found != m_models.end())
                    return found->second;
                const auto& record = asset(owner, AssetType::Model);
                auto artifact = ModelArtifact::load(m_imports.model_artifact_path(owner), owner);
                auto metadata = checked(
                    MetadataSerializer{}.load(metadata_path(m_paths.assets() / record.path)));
                if(!artifact || artifact->metadata != metadata
                    || artifact->importer_version != ModelArtifact::IMPORTER_VERSION
                    || !import_inputs_are_current(m_paths.assets(), artifact->source_inputs))
                    fail(record, "模型 CPU 产物未就绪或已过期，请等待导入完成");
                return m_models.emplace(owner, std::move(*artifact)).first->second;
            }
            std::filesystem::path product_path(const AssetRecord& record) {
                return model(record).product_path(
                    m_imports.model_artifact_path(record.source_asset), record.handle);
            }
            std::shared_ptr<const MeshData> mesh(const AssetHandle handle) {
                if(const auto found = m_meshes.find(handle); found != m_meshes.end())
                    return found->second;
                MeshData data;
                if(handle == DEFAULT_CHARACTER_MESH) {
                    data = checked(MeshImporter{}.import(default_character_source(),
                        remaining(m_limits.mesh_working_bytes), m_limits));
                } else {
                    const auto& record = asset(handle, AssetType::Mesh);
                    const auto path = record.source_asset ? product_path(record)
                                                          : m_imports.mesh_artifact_path(handle);
                    auto artifact =
                        MeshArtifact::load(path, handle, remaining(m_limits.mesh_working_bytes));
                    if(!artifact
                        || (!record.source_asset
                            && !import_inputs_are_current(
                                m_paths.assets(), artifact->source_inputs)))
                        fail(record, "网格 CPU 产物未就绪或已过期，请等待导入完成");
                    data = std::move(artifact->data);
                }
                if(!data.skin.empty())
                    throw std::runtime_error(
                        "GI 静态烘焙不接受蒙皮网格；请从烘焙输入中排除动画角色");
                reserve(data.vertices.size() * sizeof(MeshVertex)
                        + data.indices.size() * sizeof(uint32_t));
                auto result = std::make_shared<const MeshData>(std::move(data));
                m_meshes.emplace(handle, result);
                return result;
            }
            std::shared_ptr<const TextureData> texture(const AssetHandle handle) {
                if(!handle)
                    return {};
                if(const auto found = m_textures.find(handle); found != m_textures.end())
                    return found->second;
                check_cancelled();
                const auto& record = asset(handle, AssetType::Texture);
                const auto path = record.source_asset ? product_path(record)
                                                      : m_imports.texture_artifact_path(handle);
                auto artifact =
                    TextureArtifact::load(path, handle, remaining(m_limits.texture_working_bytes));
                if(!artifact)
                    fail(record, "贴图 CPU 产物未就绪，请等待导入完成");
                if(record.source_asset) {
                    const auto& samplers = model(record).texture_samplers;
                    if(const auto found = samplers.find(handle); found != samplers.end())
                        artifact->data.sampler = found->second;
                } else {
                    check_source(record, artifact->source);
                    const auto* settings =
                        std::get_if<TextureImportSettings>(&record.import_settings);
                    if(!settings || artifact->settings != *settings)
                        fail(record, "贴图导入设置已变化，请等待导入完成");
                }
                reserve(artifact->data.pixels.size());
                auto result = std::make_shared<const TextureData>(std::move(artifact->data));
                m_textures.emplace(handle, result);
                return result;
            }
            GI::Material material(const AssetHandle handle) {
                MaterialData data;
                if(const auto* builtin = find_default_asset(handle);
                    builtin && builtin->type == AssetType::Material) {
                    data.template_name = builtin->material_template;
                } else {
                    const auto& record = asset(handle, AssetType::Material);
                    const auto path =
                        record.source_asset ? product_path(record) : m_paths.assets() / record.path;
                    data = checked(MaterialSerializer{}.load(path));
                }
                const auto layout = MaterialLayout::find_builtin(data.template_name);
                if(data.shader_program || !layout)
                    throw std::runtime_error(
                        "GI 烘焙仅支持内置 PBR／无光照材质，自定义 GPU Shader 不能在 CPU 求值");
                for(const auto& property : layout->get_scalars())
                    data.scalar_properties.try_emplace(property.name, property.default_value);
                for(const auto& property : layout->get_vectors())
                    data.vector_properties.try_emplace(property.name, property.default_value);
                GI::Material result;
                result.surface = data.surface;
                if(data.template_name == "unlit_color") {
                    const auto color = data.vector_properties.at("color");
                    result.base_color = {0, 0, 0, std::clamp(color.a, 0.0f, 1.0f)};
                    result.emissive = Math::Vec3(color) * data.scalar_properties.at("intensity");
                    return result;
                }
                result.base_color = glm::clamp(
                    data.vector_properties.at("base_color"), Math::Vec4(0), Math::Vec4(1));
                result.metallic = data.scalar_properties.at("metallic");
                result.emissive = Math::Vec3(data.vector_properties.at("emissive_color"))
                                  * data.scalar_properties.at("emissive_strength");
                const auto property_texture = [&](const std::string& name) {
                    const auto found = data.texture_properties.find(name);
                    return texture(
                        found == data.texture_properties.end() ? AssetHandle{} : found->second);
                };
                result.base_color_texture = property_texture("base_color_texture");
                result.metallic_roughness_texture = property_texture("metallic_roughness_texture");
                result.emissive_texture = property_texture("emissive_texture");
                return result;
            }

            ProjectPaths m_paths;
            AssetImportLimits m_limits;
            ImportService m_imports;
            std::map<AssetHandle, AssetRecord> m_records;
            const WorkProgress& m_progress;
            std::size_t m_bytes = 0;
            std::map<AssetHandle, ModelArtifact> m_models;
            std::map<AssetHandle, std::shared_ptr<const MeshData>> m_meshes;
            std::map<AssetHandle, std::shared_ptr<const TextureData>> m_textures;
        };

        Result<IrradianceBake::Input> describe(Scene& scene, const EntityUuid uuid,
            const AssetDatabase& assets, LightingBakeInput::SourceCache* cache = nullptr) {
            using Described = Result<IrradianceBake::Input>;
            auto entity = scene.find_entity(uuid);
            if(!entity || !entity.has_component<IrradianceVolumeComponent>())
                return Described::failure("GI 烘焙区域已不存在");
            const auto& volume = entity.get_component<IrradianceVolumeComponent>();
            const Math::Vec3 center(scene.get_world_matrix(entity)[3]);
            GI::Settings settings{
                .bounds = {center - volume.half_extents, center + volume.half_extents},
                .spacing = volume.spacing,
                .max_distance = volume.max_distance,
                .ray_samples = volume.ray_samples,
                .bounces = volume.bounces};
            if(auto grid = GI::grid_dimensions(settings); !grid)
                return Described::failure(grid.error());
            auto common = LightingBakeInput::describe(
                scene, assets, LightingBakeInput::Purpose::Irradiance, 0, cache);
            if(!common)
                return Described::failure(common.error());
            Binary::Writer writer;
            writer.write_string(common.value().fingerprint, 64);
            writer.write_u32(GI::VERSION);
            for(const auto& point : {settings.bounds.minimum, settings.bounds.maximum})
                for(int axis = 0; axis < 3; ++axis)
                    writer.write_float(point[axis]);
            writer.write_float(settings.spacing);
            writer.write_float(settings.max_distance);
            writer.write_u32(settings.ray_samples);
            writer.write_u32(settings.bounces);
            return Described::success({std::move(common.value().scene), settings,
                std::to_string(Binary::hash_bytes(writer.data())),
                std::move(common.value().dependencies)});
        }

        class BakeCommand final: public CommandHistory::Command {
        public:
            BakeCommand(EntityUuid entity, IrradianceVolumeComponent before,
                IrradianceVolumeComponent after)
                : m_entity(entity), m_before(std::move(before)), m_after(std::move(after)) {}
            bool undo(Scene& scene) override { return apply(scene, m_before); }
            bool redo(Scene& scene) override { return apply(scene, m_after); }

        private:
            bool apply(Scene& scene, const IrradianceVolumeComponent& value) const {
                auto entity = scene.find_entity(m_entity);
                return entity && entity.try_set_component(value);
            }
            EntityUuid m_entity;
            IrradianceVolumeComponent m_before;
            IrradianceVolumeComponent m_after;
        };
    }

    struct IrradianceBake::Job {
        std::future<Result<WorkOutput>> completion;
        std::shared_ptr<WorkProgress> progress;
        EntityUuid entity;
        uint64_t lifetime;
        uint64_t generation;
        std::string fingerprint;
        std::vector<std::pair<AssetHandle, uint64_t>> versions;
    };

    IrradianceBake::IrradianceBake(
        EditorAssets& assets, const AssetRegistry& registry, TaskScheduler& scheduler)
        : m_assets(assets), m_registry(registry), m_scheduler(scheduler) {}
    IrradianceBake::~IrradianceBake() {
        cancel();
    }

    Result<IrradianceBake::Input> IrradianceBake::describe_input(
        Scene& scene, EntityUuid uuid, const AssetDatabase& assets) {
        return describe(scene, uuid, assets);
    }

    Result<void> IrradianceBake::start(
        Scene& scene, const EntityUuid entity, const CommandHistory& history) {
        if(busy())
            return Result<void>::failure("已有 GI 任务正在烘焙");
        if(history.get_scene() != &scene)
            return Result<void>::failure("只能烘焙当前编辑场景");
        auto input = describe_input(scene, entity, m_assets.database());
        if(!input)
            return Result<void>::failure(input.error());
        std::map<AssetHandle, AssetRecord> records;
        std::vector<std::pair<AssetHandle, uint64_t>> versions;
        for(const auto handle : input.value().dependencies) {
            if(const auto* record = m_assets.database().find(handle))
                records.emplace(handle, *record);
            versions.emplace_back(handle, m_registry.get_revision(handle));
        }
        auto progress = std::make_shared<WorkProgress>();
        const auto grid = GI::grid_dimensions(input.value().settings).value();
        progress->total = grid.x * grid.y * grid.z;
        const auto fingerprint = input.value().fingerprint;
        auto task = m_scheduler.try_submit_result(
            [input = std::move(input).value(), paths = m_assets.database().paths(),
                limits = m_assets.import_limits(), records = std::move(records),
                progress]() mutable -> Result<WorkOutput> {
                try {
                    const auto start = std::chrono::steady_clock::now();
                    SourceReader reader(std::move(paths), limits, std::move(records), *progress);
                    auto source = reader.read(input.scene);
                    auto baked = GI::bake(source, input.settings,
                        [&](const uint32_t completed, const uint32_t total) {
                            progress->completed.store(completed, std::memory_order_relaxed);
                            progress->total.store(total, std::memory_order_relaxed);
                            return !progress->cancelled.load(std::memory_order_relaxed);
                        });
                    if(!baked)
                        return Result<WorkOutput>::failure(baked.error());
                    if(baked.value().statistics.active_probes == 0)
                        return Result<WorkOutput>::failure(
                            "没有有效 GI 探针；请将区域放在空旷空间而非实体内部");
                    Report report{baked.value().statistics, reader.input_bytes(),
                        baked.value().data.probes.size() * sizeof(IrradianceProbe),
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                            .count()};
                    return Result<WorkOutput>::success({std::move(baked.value().data), report});
                } catch(const std::exception& error) {
                    return Result<WorkOutput>::failure(error.what());
                }
            });
        if(!task)
            return Result<void>::failure("后台任务队列已满，请稍后重试 GI 烘焙");
        m_job = std::make_unique<Job>(Job{std::move(*task), std::move(progress), entity,
            scene.get_lifetime(), history.generation(), fingerprint, std::move(versions)});
        m_report.reset();
        return Result<void>::success();
    }

    Result<bool> IrradianceBake::update(Scene& scene, CommandHistory& history) {
        if(!m_job)
            return Result<bool>::success(false);
        if(scene.get_lifetime() != m_job->lifetime || history.get_scene() != &scene
            || history.generation() != m_job->generation) {
            cancel();
            return Result<bool>::success(false);
        }
        if(m_job->completion.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return Result<bool>::success(false);
        auto job = std::move(m_job);
        auto result = job->completion.get();
        if(!result)
            return Result<bool>::failure(result.error());
        auto input = describe_input(scene, job->entity, m_assets.database());
        if(!input || input.value().fingerprint != job->fingerprint
            || std::ranges::any_of(job->versions, [&](const auto& version) {
                   return m_registry.get_revision(version.first) != version.second;
               }))
            return Result<bool>::failure("烘焙期间场景或资源已变化，请重新烘焙 GI");
        const auto path =
            std::filesystem::path("baked")
            / ("gi_" + job->entity.to_string() + "_" + Uuid::generate().to_string() + ".gi");
        auto report = m_assets.create_irradiance(path, result.value().data);
        if(!report.succeeded())
            return Result<bool>::failure(report.issues.front().message);
        const auto* record = m_assets.database().find(path);
        if(!record)
            return Result<bool>::failure("GI 产物未能加入资产索引");
        auto entity = scene.find_entity(job->entity);
        const auto before = entity.get_component<IrradianceVolumeComponent>();
        auto after = before;
        after.irradiance = record->handle;
        after.bake_fingerprint = job->fingerprint + ":" + std::to_string(record->handle.value());
        if(!history.execute(std::make_unique<BakeCommand>(job->entity, before, std::move(after))))
            return Result<bool>::failure("无法记录 GI 烘焙撤销命令");
        m_report = result.value().report;
        m_status.clear();
        return Result<bool>::success(true);
    }

    std::string IrradianceBake::status(Scene& scene, const EntityUuid uuid) {
        if(m_job && m_job->entity == uuid)
            return "正在后台烘焙 GI…";
        const LightingBakeInput::StatusKey key{uuid, scene.get_lifetime(),
            scene.get_render_revision(), m_assets.database().generation(),
            m_registry.get_revision(), GI::VERSION};
        if(const auto* cached = m_status.find(key))
            return *cached;
        auto entity = scene.find_entity(uuid);
        if(!entity || !entity.has_component<IrradianceVolumeComponent>())
            return {};
        const auto& volume = entity.get_component<IrradianceVolumeComponent>();
        std::string text;
        if(volume.bake_fingerprint.empty())
            text = "尚未烘焙；区域按世界坐标保存，移动后需重新烘焙";
        else {
            auto input = describe(scene, uuid, m_assets.database(),
                &m_status.sources(m_assets.database().generation()));
            if(!input)
                text = input.error();
            else if(input.value().fingerprint + ":" + std::to_string(volume.irradiance.value())
                    != volume.bake_fingerprint)
                text = "GI 已过期：静态内容、光照、区域或烘焙设置发生变化";
            else if(!m_assets.database().find(volume.irradiance))
                text = "GI 产物缺失，请重新烘焙";
            else
                text = "烘焙输入未变化";
        }
        m_status.store(key, text);
        return text;
    }

    bool IrradianceBake::busy() const {
        return bool(m_job);
    }
    IrradianceBake::Progress IrradianceBake::progress() const {
        return m_job ? Progress{m_job->progress->completed.load(std::memory_order_relaxed),
                           m_job->progress->total.load(std::memory_order_relaxed)}
                     : Progress{};
    }
    void IrradianceBake::cancel() {
        if(m_job)
            m_job->progress->cancelled.store(true, std::memory_order_relaxed);
        m_job.reset();
        m_status.clear();
    }
}
