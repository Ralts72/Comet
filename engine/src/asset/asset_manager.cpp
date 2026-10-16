#include "asset/asset_manager.h"
#include "asset/data/animation_library.h"
#include "asset/data/game_data.h"
#include "audio/audio.h"
#include "animation/animation_controller.h"
#include "asset/default_assets.h"
#include "scene/component_registry.h"
#include "asset/runtime/asset_loader.h"
#include "asset/runtime/render_asset_factory.h"
#include "graphics/error.h"
#include "asset/import/asset_task_queue.h"
#include "asset/import/import_candidate.h"
#include "common/result.h"
#include "asset/data/texture_data.h"

#include "asset/artifact/mesh_artifact.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/import/import_service.h"
#include "asset/import/mesh_importer.h"
#include "asset/import/texture_importer.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "common/file_io.h"
#include "diagnostics/logger.h"

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Comet {
    namespace {
        bool validate_asset_handle(const AssetHandle handle, const std::string_view operation) {
            if(handle) {
                return true;
            }
            LOG_ERROR("Cannot {} with an invalid asset handle", operation);
            return false;
        }

        const AssetRecord* find_asset_record(const AssetDatabase& database,
            const AssetHandle handle, const AssetType expected_type) {
            const AssetRecord* record = database.find(handle);
            if(!record) {
                LOG_ERROR("Asset handle {} is not indexed", handle.value());
                return nullptr;
            }
            if(record->type != expected_type) {
                LOG_ERROR("Asset handle {} has type '{}', expected '{}'", handle.value(),
                    to_string(record->type), to_string(expected_type));
                return nullptr;
            }
            return record;
        }

        template<typename T> struct RuntimeAssetLookup {
            std::shared_ptr<T> asset;
            bool type_conflict = false;
        };

        template<typename T>
        RuntimeAssetLookup<T> find_runtime_asset(
            const AssetRegistry& registry, const AssetHandle handle, std::shared_ptr<T> asset) {
            if(asset) {
                return {.asset = std::move(asset)};
            }
            if(registry.contains(handle)) {
                LOG_ERROR("Asset handle {} is already registered with another runtime type",
                    handle.value());
                return {.type_conflict = true};
            }
            return {};
        }

    }

    AssetManager::AssetManager(ProjectPaths paths, AssetRegistry& registry,
        RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler)
        : AssetManager(
              std::move(paths), registry, resource_factory, task_scheduler, AssetImportLimits{}) {}

    AssetManager::AssetManager(ProjectPaths paths, AssetRegistry& registry,
        RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler,
        const AssetImportLimits limits)
        : m_limits(limits), m_owned_database(std::make_unique<AssetDatabase>(std::move(paths))),
          m_database(*m_owned_database),
          m_import_service(std::make_unique<ImportService>(m_database.paths(), m_limits)),
          m_registry(registry),
          m_render_factory(std::make_unique<RenderAssetFactory>(resource_factory)),
          m_loader(std::make_unique<AssetLoader>(m_database, registry, *m_import_service,
              *m_render_factory, limits.mesh_working_bytes, limits.texture_working_bytes,
              limits.async.working_bytes)),
          m_task_queue(std::make_unique<AssetTaskQueue>(m_database, task_scheduler, limits.async)) {
    }

    AssetManager::AssetManager(AssetDatabase& database, AssetRegistry& registry,
        RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler,
        const AssetImportLimits limits)
        : m_limits(limits), m_database(database),
          m_import_service(std::make_unique<ImportService>(m_database.paths(), m_limits)),
          m_registry(registry),
          m_render_factory(std::make_unique<RenderAssetFactory>(resource_factory)),
          m_loader(std::make_unique<AssetLoader>(m_database, registry, *m_import_service,
              *m_render_factory, limits.mesh_working_bytes, limits.texture_working_bytes,
              limits.async.working_bytes)),
          m_task_queue(std::make_unique<AssetTaskQueue>(m_database, task_scheduler, limits.async)) {
    }

    AssetManager::~AssetManager() = default;

    AssetManager::Residency AssetManager::residency() const {
        Residency result;
        const auto models = m_loader->model_residency();
        result.model_cache_entries = models.count;
        result.model_payload_bytes = models.payload_bytes;
        RenderAssetFactory::ResidencyInput render_resources;
        std::unordered_set<const AudioClip*> clips;
        std::unordered_set<const ShaderProgramArtifact*> programs;
        for(const auto handle : m_registry.handles()) {
            ++result.registry_assets;
            const auto* record = m_database.find(handle);
            const auto* builtin = find_default_asset(handle);
            auto type = AssetType::Unknown;
            if(record)
                type = record->type;
            else if(builtin)
                type = builtin->type;
            ++result.registry_counts[type];
            if(auto mesh = m_registry.resolve<Mesh>(handle))
                render_resources.meshes.push_back(std::move(mesh));
            if(auto texture = m_registry.resolve<Texture>(handle))
                render_resources.textures.push_back(std::move(texture));
            if(auto environment = m_registry.resolve<Environment>(handle))
                render_resources.environments.push_back(std::move(environment));
            if(auto irradiance = m_registry.resolve<Irradiance>(handle))
                render_resources.irradiance.push_back(std::move(irradiance));
            if(const auto clip = m_registry.resolve<AudioClip>(handle);
                clip && clips.insert(clip.get()).second)
                result.audio_pcm_bytes += clip->samples().size_bytes();
            if(const auto program = m_registry.resolve<ShaderProgramArtifact>(handle);
                program && programs.insert(program.get()).second)
                result.shader_code_bytes +=
                    (program->vertex_words.size() + program->fragment_words.size())
                    * sizeof(uint32_t);
        }
        const auto gpu = RenderAssetFactory::resident_bytes(render_resources);
        result.mesh_buffer_bytes = gpu.mesh_buffer_bytes;
        result.texture_pixel_bytes = gpu.texture_pixel_bytes;
        return result;
    }

    AssetManager::ResidencyRelease AssetManager::release_unused(
        const std::span<const AssetReference> references,
        const std::span<const AssetHandle> retained_roots) {
        std::unordered_set<AssetHandle> retained(retained_roots.begin(), retained_roots.end());
        for(const auto& reference : references)
            retained.insert(reference.handle);
        std::vector<AssetHandle> pending(retained.begin(), retained.end());
        while(!pending.empty()) {
            const auto handle = pending.back();
            pending.pop_back();
            const auto* record = m_database.find(handle);
            if(!record)
                continue;
            const auto retain = [&](AssetHandle dependency) {
                if(dependency && retained.insert(dependency).second)
                    pending.push_back(dependency);
            };
            retain(record->source_asset);
            for(const auto dependency : m_database.get_dependencies(handle))
                retain(dependency);
        }
        ResidencyRelease released;
        for(const auto& record : m_database.get_assets()) {
            if(find_default_asset(record.handle) || retained.contains(record.handle)) {
                m_released_assets.erase(record.handle);
                continue;
            }
            m_released_assets.insert(record.handle);
            released.registry_assets += m_registry.unregister_asset(record.handle);
            m_refresh_requests.erase(record.handle);
            m_environment_previews.erase(record.handle);
            m_failed_environments.erase(record.handle);
        }
        released.model_cache_entries = m_loader->release_models_except(retained);
        return released;
    }

    Result<void> AssetManager::update_import_dependencies(
        const AssetHandle handle, std::vector<std::filesystem::path> dependencies) {
        // 导出产品的依赖已固定；加载 Lua 时无需维护编辑器的源文件观察列表。
        if(m_database.is_read_only())
            return Result<void>::success();
        return m_database.update_import_dependencies(handle, std::move(dependencies));
    }

    std::shared_ptr<const ShaderProgramArtifact> AssetManager::compiled_shader_program(
        const AssetHandle handle) const {
        return m_registry.resolve<ShaderProgramArtifact>(handle);
    }

    AssetScanReport AssetManager::scan() {
        AssetScanReport report = m_database.scan();
        accept_scan_report(report);
        return report;
    }

    Result<void, Error> AssetManager::ensure_loaded(
        const AssetHandle handle, const AssetType expected_type) {
        m_released_assets.erase(handle);
        const auto status = [](const auto& loaded) {
            if(!loaded)
                return Result<void, Error>::failure(loaded.error());
            return Result<void, Error>::success();
        };
        switch(expected_type) {
            case AssetType::Irradiance:
                return status(load_irradiance(handle));
            case AssetType::AnimationLibrary:
                return status(load_animation_library(handle));
            case AssetType::Data:
                return status(load_data(handle));
            case AssetType::AnimationController:
                return status(load_animation_controller(handle));
            case AssetType::Prefab:
                return status(load_prefab(handle, create_scene_component_registry()));
            case AssetType::Model:
                return status(load_model(handle));
            case AssetType::Audio:
                return status(load_audio(handle));
            case AssetType::Script:
                return status(load_script(handle));
            case AssetType::Environment:
                return status(load_environment(handle));
            case AssetType::Mesh:
                return status(load_mesh(handle));
            case AssetType::Material:
                return status(load_material(handle));
            case AssetType::Texture:
                return status(load_texture(handle));
            case AssetType::ShaderProgram:
                return status(load_shader_program(handle));
            default:
                return Result<void, Error>::failure(
                    {"Runtime loading is not supported for this asset type"});
        }
    }

    Result<AssetManager::EnvironmentState, Error> AssetManager::environment_state(
        const AssetHandle handle) const {
        const auto* record = m_database.find(handle);
        if(!record || record->type != AssetType::Environment)
            return Result<EnvironmentState, Error>::failure(
                {"Environment is not indexed: " + std::to_string(handle.value())});
        const auto environment = m_registry.resolve<Environment>(handle);
        // 重载失败不影响已发布的完整版本；低清预览不算就绪。
        if(RenderAssetFactory::has_lighting(environment))
            return Result<EnvironmentState, Error>::success(EnvironmentState::Ready);
        if(!environment && m_registry.contains(handle))
            return Result<EnvironmentState, Error>::failure({"Runtime environment type conflict"});
        const auto revision = m_database.get_revision(handle);
        if(const auto failed = m_failed_environments.find(handle);
            failed != m_failed_environments.end() && failed->second == revision)
            return Result<EnvironmentState, Error>::success(EnvironmentState::Failed);
        const auto deferred = m_refresh_requests.find(handle);
        if(m_task_queue->contains(handle, revision)
            || (deferred != m_refresh_requests.end() && deferred->second == revision))
            return Result<EnvironmentState, Error>::success(EnvironmentState::Preparing);
        if(environment)
            return Result<EnvironmentState, Error>::success(EnvironmentState::Failed);
        return Result<EnvironmentState, Error>::success(EnvironmentState::Unloaded);
    }

    Result<void, Error> AssetManager::request_load(
        const AssetHandle handle, const AssetType expected_type) {
        m_released_assets.erase(handle);
        if(expected_type != AssetType::Environment)
            return ensure_loaded(handle, expected_type);
        const auto state = environment_state(handle);
        if(!state)
            return Result<void, Error>::failure(state.error());
        if(state.value() == EnvironmentState::Ready || state.value() == EnvironmentState::Preparing)
            return Result<void, Error>::success();
        if(state.value() == EnvironmentState::Failed)
            return Result<void, Error>::failure(
                {"Environment preparation failed; waiting for source changes"});
        auto scheduled = schedule_environment(*m_database.find(handle));
        if(!scheduled)
            return Result<void, Error>::failure(scheduled.error());
        if(!scheduled.value())
            m_refresh_requests[handle] = m_database.get_revision(handle);
        return Result<void, Error>::success();
    }

    Result<void, Error> AssetManager::request_load(const AssetReference& reference) {
        if(!reference.cpu_only)
            return request_load(reference.handle, reference.type);
        if(reference.type != AssetType::Mesh)
            return Result<void, Error>::failure({"CPU-only loading requires a mesh asset"});
        auto data = read_mesh_data(reference.handle);
        if(!data)
            return Result<void, Error>::failure(data.error());
        return Result<void, Error>::success();
    }

    Result<std::shared_ptr<AnimationLibrary>, Error> AssetManager::load_animation_library(
        AssetHandle handle) {
        using Loaded = Result<std::shared_ptr<AnimationLibrary>, Error>;
        if(!m_database.is_read_only() && !m_registry.contains<AnimationLibrary>(handle)) {
            const auto* record = m_database.find(handle);
            if(record && record->type == AssetType::AnimationLibrary) {
                const auto dependencies = record->dependencies;
                std::unordered_set<AssetHandle> imported;
                for(const auto mesh : dependencies) {
                    const auto* source = m_database.find(mesh);
                    const auto owner = source && source->source_asset ? source->source_asset : mesh;
                    if(!imported.insert(owner).second)
                        continue;
                    if(auto prepared = import_mesh(mesh); !prepared)
                        return Loaded::failure(prepared.error());
                }
            }
        }
        return m_loader->load_animation_library(handle);
    }
    Result<std::shared_ptr<AnimationController>, Error> AssetManager::load_animation_controller(
        AssetHandle handle) {
        if(!m_registry.contains<AnimationController>(handle)) {
            if(const auto* record = m_database.find(handle);
                record && record->type == AssetType::AnimationController) {
                const auto dependencies = record->dependencies;
                for(const auto library : dependencies)
                    if(auto loaded = load_animation_library(library); !loaded)
                        return Result<std::shared_ptr<AnimationController>, Error>::failure(
                            loaded.error());
            }
        }
        return m_loader->load_animation_controller(handle);
    }

    Result<std::shared_ptr<GameData>, Error> AssetManager::load_data(AssetHandle handle) {
        return m_loader->load_data(handle);
    }

    Result<std::shared_ptr<Irradiance>, Error> AssetManager::load_irradiance(AssetHandle handle) {
        return m_loader->load_irradiance(handle);
    }

    Result<std::shared_ptr<Prefab>, Error> AssetManager::load_prefab(
        AssetHandle handle, const ComponentRegistry& components) {
        return m_loader->load_prefab(handle, components);
    }

    Result<std::vector<AssetReference>, Error> AssetManager::collect_scene_references(
        Scene& scene, const ComponentRegistry& components, MissingAssetPolicy policy) {
        return components.collect_runtime_references(
            scene, [this](AssetHandle handle) { return load_script(handle); },
            [this, &components](AssetHandle handle) { return load_prefab(handle, components); },
            [this](AssetHandle handle) { return load_data(handle); },
            policy == MissingAssetPolicy::AllowMissing);
    }

    Result<std::vector<AssetReference>, Error> AssetManager::prepare_scene(
        Scene& scene, const ComponentRegistry& components) {
        using Prepared = Result<std::vector<AssetReference>, Error>;
        auto collected = collect_scene_references(scene, components);
        if(!collected)
            return Prepared::failure(collected.error());
        auto references = std::move(collected).value();
        if(!m_database.is_read_only()) {
            std::unordered_set<AssetHandle> imported;
            for(const auto& reference : references) {
                const auto* record = m_database.find(reference.handle);
                if(record && record->source_asset) {
                    const auto source = record->source_asset;
                    if(!imported.insert(source).second)
                        continue;
                    if(auto result = import_model(source); !result)
                        return Prepared::failure(result.error());
                } else if(reference.type == AssetType::Mesh
                          && imported.insert(reference.handle).second) {
                    if(auto result = import_mesh(reference.handle); !result)
                        return Prepared::failure(result.error());
                }
            }
        }
        if(auto prepared = prepare_references(references, MissingAssetPolicy::FailRequired);
            !prepared)
            return Prepared::failure(prepared.error());
        return Prepared::success(std::move(references));
    }

    Result<std::size_t, Error> AssetManager::prepare_references(
        const std::span<const AssetReference> references, const MissingAssetPolicy policy) {
        std::size_t missing = 0;
        for(const auto& reference : references) {
            auto loaded = request_load(reference);
            if(loaded)
                continue;
            if(is_device_lost(loaded.error())
                || (reference.required && policy == MissingAssetPolicy::FailRequired))
                return Result<std::size_t, Error>::failure(loaded.error());
            LOG_WARN("Unresolved asset {}: {}", reference.handle.value(), loaded.error().message);
            ++missing;
        }
        return Result<std::size_t, Error>::success(missing);
    }

    Result<bool, Error> AssetManager::references_ready(
        const std::span<const AssetReference> references, const MissingAssetPolicy policy) const {
        bool ready = true;
        for(const auto& reference : references) {
            if(reference.cpu_only) {
                const auto prepared = m_mesh_data.find(reference.handle);
                if(reference.type == AssetType::Mesh && prepared != m_mesh_data.end()
                    && prepared->second.available && prepared->second.prepared)
                    continue;
                if(reference.required && policy == MissingAssetPolicy::FailRequired)
                    return Result<bool, Error>::failure({"CPU mesh data is not prepared"});
                continue;
            }
            if(const auto* asset = find_default_asset(reference.handle);
                asset && asset->type == reference.type) {
                const bool loaded =
                    (asset->type == AssetType::Mesh && m_registry.contains<Mesh>(reference.handle))
                    || (asset->type == AssetType::Material
                        && m_registry.contains<Material>(reference.handle))
                    || (asset->type == AssetType::AnimationController
                        && m_registry.contains<AnimationController>(reference.handle));
                if(loaded)
                    continue;
            }
            if(reference.type == AssetType::Environment) {
                const auto state = environment_state(reference.handle);
                if(state && state.value() == EnvironmentState::Ready)
                    continue;
                if(state && state.value() == EnvironmentState::Preparing) {
                    ready = false;
                    continue;
                }
            } else {
                const auto* record = m_database.find(reference.handle);
                if(record && record->type == reference.type) {
                    if((reference.type == AssetType::Model
                           && m_loader->model_is_loaded(reference.handle))
                        || m_registry.contains(reference.handle))
                        continue;
                    const auto revision = m_database.get_revision(reference.handle);
                    const auto deferred = m_refresh_requests.find(reference.handle);
                    if(m_task_queue->contains(reference.handle, revision)
                        || (deferred != m_refresh_requests.end() && deferred->second == revision)) {
                        ready = false;
                        continue;
                    }
                }
            }
            if(reference.required && policy == MissingAssetPolicy::FailRequired)
                return Result<bool, Error>::failure(
                    {"Required asset is not ready: " + std::to_string(reference.handle.value())});
        }
        return Result<bool, Error>::success(ready);
    }

    AssetManager::ImportPublication AssetManager::publish_import_result(AssetImportResult& result) {
        if(auto* candidate = std::get_if<ModelImportCandidate>(&result.candidate))
            return publish_model_candidate(*candidate);
        if(auto* candidate = std::get_if<MeshArtifactCandidate>(&result.candidate))
            return publish_mesh_candidate(*candidate);
        if(auto* candidate = std::get_if<MaterialImportCandidate>(&result.candidate))
            return publish_material_candidate(*candidate);
        if(auto* candidate = std::get_if<TextureImportCandidate>(&result.candidate))
            return publish_texture_candidate(*candidate);
        if(auto* candidate = std::get_if<EnvironmentImportCandidate>(&result.candidate))
            return publish_environment_candidate(*candidate);
        if(auto* candidate = std::get_if<ShaderProgramImportCandidate>(&result.candidate))
            return publish_shader_program_candidate(*candidate);
        return ImportPublication::failure({"Import task completed without a candidate"});
    }

    AssetManager::ImportPublication AssetManager::publish_shader_program_candidate(
        ShaderProgramImportCandidate& candidate) {
        const auto& request = candidate.request;
        const auto handle = request.handle;
        const auto* program = m_database.find(handle);
        const auto* vertex = m_database.find(request.vertex.handle);
        const auto* fragment = m_database.find(request.fragment.handle);
        if(!program || program->type != AssetType::ShaderProgram || !vertex
            || vertex->type != AssetType::Shader || !fragment || fragment->type != AssetType::Shader
            || !m_database.is_current(handle, request.revision)
            || !m_database.is_current(request.vertex.handle, request.vertex.revision)
            || !m_database.is_current(request.fragment.handle, request.fragment.revision))
            return ImportPublication::success(std::nullopt);
        if(!candidate.result) {
            std::vector<std::filesystem::path> dependencies(
                m_database.get_import_dependencies(handle).begin(),
                m_database.get_import_dependencies(handle).end());
            dependencies.insert(dependencies.end(), candidate.result.error().dependencies.begin(),
                candidate.result.error().dependencies.end());
            if(auto indexed = update_import_dependencies(handle, std::move(dependencies)); !indexed)
                LOG_WARN("Shader program {} dependencies: {}", handle.value(), indexed.error());
            LOG_ERROR("Shader program {}: {}", handle.value(), candidate.result.error().message);
            invalidate_shader_program(handle);
            return ImportPublication::success(std::nullopt);
        }

        auto& prepared = candidate.result.value();
        auto& artifact = prepared.artifact;
        const auto descriptor =
            request.descriptor_path.lexically_relative(m_database.paths().assets());
        if(artifact.handle != handle || artifact.inputs.files.empty()
            || artifact.inputs.files.front().relative_path != descriptor
            || !import_inputs_are_current(m_database.paths().assets(), artifact.inputs)) {
            LOG_WARN("Shader program {} candidate is stale or invalid", handle.value());
            return ImportPublication::success(std::nullopt);
        }
        if(!prepared.from_cache) {
            if(auto saved =
                    artifact.publish_atomic(m_import_service->shader_program_artifact_path(handle));
                !saved) {
                LOG_WARN("Shader program {}: {}", handle.value(), saved.error());
                return ImportPublication::success(std::nullopt);
            }
        }

        std::vector<std::filesystem::path> dependencies;
        for(const auto& file : artifact.inputs.files)
            if(file.relative_path != descriptor)
                dependencies.push_back(file.relative_path);
        if(auto indexed = update_import_dependencies(handle, std::move(dependencies)); !indexed) {
            LOG_WARN("Shader program {} dependencies: {}", handle.value(), indexed.error());
            return ImportPublication::success(std::nullopt);
        }
        if(m_released_assets.contains(handle) && !m_registry.contains(handle))
            return ImportPublication::success(handle);
        auto version = std::make_shared<ShaderProgramArtifact>(std::move(artifact));
        bool stored = false;
        if(m_registry.contains(handle))
            stored = m_registry.replace_asset(handle, version);
        else
            stored = m_registry.register_asset(handle, version);
        if(!stored) {
            LOG_WARN("Shader program {} cannot replace the registered asset", handle.value());
            return ImportPublication::success(std::nullopt);
        }
        return ImportPublication::success(handle);
    }

    void AssetManager::invalidate_shader_program(const AssetHandle handle) {
        if(m_registry.contains<ShaderProgramArtifact>(handle))
            static_cast<void>(m_registry.unregister_asset(handle));
    }

    AssetManager::ImportPublication AssetManager::publish_mesh_candidate(
        MeshArtifactCandidate& candidate) {
        if(!candidate.result) {
            change_mesh_data(candidate.handle, false);
            LOG_ERROR("Failed to prepare mesh artifact '{}' (handle {}): {}",
                candidate.relative_path.generic_string(), candidate.handle.value(),
                candidate.result.error());
            return ImportPublication::success(std::nullopt);
        }
        auto& artifact = candidate.result.value();
        if(candidate.reused_artifact) {
            complete_mesh_import(
                candidate.handle, candidate.revision, artifact.source_dependencies());
            return ImportPublication::success(std::nullopt);
        }
        if(auto publication =
                artifact.publish_atomic(m_import_service->mesh_artifact_path(candidate.handle));
            !publication) {
            LOG_ERROR("Failed to publish mesh artifact '{}' (handle {}): {}",
                candidate.relative_path.generic_string(), candidate.handle.value(),
                publication.error());
            return ImportPublication::success(std::nullopt);
        }
        complete_mesh_import(candidate.handle, candidate.revision, artifact.source_dependencies());

        // Artifact 已发布；后续普通 GPU 创建失败不撤销这个事实。
        if(auto refreshed =
                refresh_loaded_mesh(candidate.handle, candidate.revision, std::move(artifact.data));
            !refreshed) {
            if(is_device_lost(refreshed.error()))
                return ImportPublication::failure(refreshed.error());
            LOG_ERROR("Failed to refresh mesh handle {}: {}", candidate.handle.value(),
                refreshed.error().message);
            return ImportPublication::success(candidate.handle);
        }
        LOG_INFO("Imported mesh artifact '{}' (handle {})",
            candidate.relative_path.generic_string(), candidate.handle.value());
        return ImportPublication::success(candidate.handle);
    }

    AssetManager::ImportPublication AssetManager::publish_material_candidate(
        MaterialImportCandidate& candidate) {
        const auto handle = candidate.record.handle;
        if(!candidate.result) {
            LOG_ERROR("Failed to read modified material handle {}: {}", handle.value(),
                candidate.result.error());
            return ImportPublication::success(std::nullopt);
        }
        const auto runtime =
            find_runtime_asset(m_registry, handle, m_registry.resolve<Material>(handle));
        if(!runtime.asset)
            return ImportPublication::success(std::nullopt);
        const auto expected_publication = m_registry.get_revision(handle);
        auto material = m_loader->prepare_material(candidate.record, candidate.result.value());
        if(!material) {
            if(is_device_lost(material.error()))
                return ImportPublication::failure(material.error());
            LOG_ERROR(
                "Failed to refresh material {}: {}", handle.value(), material.error().message);
            return ImportPublication::success(std::nullopt);
        }
        if(!m_database.is_current(handle, candidate.revision)
            || m_registry.get_revision(handle) != expected_publication)
            return ImportPublication::success(std::nullopt);
        if(auto publication =
                publish_material(handle, candidate.result.value(), material.value(), true);
            !publication) {
            LOG_ERROR(
                "Failed to publish material {}: {}", handle.value(), publication.error().message);
            return ImportPublication::success(std::nullopt);
        }
        return ImportPublication::success(handle);
    }

    AssetManager::ImportPublication AssetManager::publish_texture_candidate(
        TextureImportCandidate& candidate) {
        if(m_released_assets.contains(candidate.handle) && !m_registry.contains(candidate.handle))
            return ImportPublication::success(std::nullopt);
        if(!candidate.result) {
            LOG_ERROR("Failed to prepare texture asset '{}' (handle {}): {}",
                candidate.relative_path.generic_string(), candidate.handle.value(),
                candidate.result.error());
            return ImportPublication::success(std::nullopt);
        }
        const auto publication = m_registry.get_revision(candidate.handle);
        auto texture_attempt = m_render_factory->prepare(std::move(candidate.result).value());
        if(!texture_attempt) {
            if(is_device_lost(texture_attempt.error()))
                return ImportPublication::failure(texture_attempt.error());
            LOG_ERROR("Failed to create refreshed runtime texture for asset handle {}: {}",
                candidate.handle.value(), texture_attempt.error().message);
            return ImportPublication::success(std::nullopt);
        }
        auto texture = std::move(texture_attempt).value();
        if(!m_database.is_current(candidate.handle, candidate.revision)
            || m_registry.get_revision(candidate.handle) != publication) {
            LOG_DEBUG("Discarded stale runtime texture candidate for asset handle {} (revision {})",
                candidate.handle.value(), candidate.revision);
            return ImportPublication::success(std::nullopt);
        }
        const bool published = m_registry.replace_asset(candidate.handle, texture);
        if(!published) {
            LOG_ERROR("Failed to publish refreshed runtime texture for asset handle {}",
                candidate.handle.value());
            return ImportPublication::success(std::nullopt);
        }
        if(auto refreshed = reload_loaded_material_dependents(candidate.handle); !refreshed)
            return ImportPublication::failure(refreshed.error());
        LOG_INFO("Published texture asset '{}' (handle {})",
            candidate.relative_path.generic_string(), candidate.handle.value());
        return ImportPublication::success(candidate.handle);
    }

    AssetManager::ImportPublication AssetManager::publish_environment_candidate(
        EnvironmentImportCandidate& candidate) {
        m_environment_previews.erase(candidate.handle);
        if(m_released_assets.contains(candidate.handle) && !m_registry.contains(candidate.handle))
            return ImportPublication::success(std::nullopt);
        const auto publication = m_registry.get_revision(candidate.handle);
        // 失败时移除临时预览，不把它当成最后一个成功版本。
        const auto mark_failed = [this, &candidate] {
            m_failed_environments[candidate.handle] = candidate.revision;
            const auto current = m_registry.resolve<Environment>(candidate.handle);
            if(current && !RenderAssetFactory::has_lighting(current))
                static_cast<void>(m_registry.unregister_asset(candidate.handle));
        };
        if(!candidate.result) {
            mark_failed();
            LOG_ERROR("Failed to prepare environment '{}': {}",
                candidate.relative_path.generic_string(), candidate.result.error());
            return ImportPublication::success(std::nullopt);
        }
        auto environment = m_render_factory->prepare(std::move(candidate.result).value());
        const bool current = m_database.is_current(candidate.handle, candidate.revision)
                             && m_registry.get_revision(candidate.handle) == publication;
        if(!environment) {
            if(current)
                mark_failed();
            if(is_device_lost(environment.error()))
                return ImportPublication::failure(environment.error());
            if(!current)
                return ImportPublication::success(std::nullopt);
            LOG_ERROR("Failed to create environment {}: {}", candidate.handle.value(),
                environment.error().message);
            return ImportPublication::success(std::nullopt);
        }
        if(!current)
            return ImportPublication::success(std::nullopt);
        bool published = false;
        if(m_registry.contains(candidate.handle))
            published = m_registry.replace_asset(candidate.handle, environment.value());
        else
            published = m_registry.register_asset(candidate.handle, environment.value());
        if(!published) {
            LOG_ERROR("Failed to publish environment {}", candidate.handle.value());
            return ImportPublication::success(std::nullopt);
        }
        m_failed_environments.erase(candidate.handle);
        LOG_INFO("Published environment asset '{}' (handle {})",
            candidate.relative_path.generic_string(), candidate.handle.value());
        return ImportPublication::success(candidate.handle);
    }

    void AssetManager::invalidate_model(AssetHandle handle) {
        std::unordered_set<AssetHandle> invalidated{handle};
        m_database.include_dependents(invalidated);
        for(const auto child : invalidated) {
            const auto* record = m_database.find(child);
            if((record && record->type == AssetType::Mesh) || m_mesh_data.contains(child))
                change_mesh_data(child, false);
            static_cast<void>(m_registry.unregister_asset(child));
            m_refresh_requests.erase(child);
        }
        m_loader->invalidate_model(handle);
    }

    AssetManager::ImportPublication AssetManager::publish_model_candidate(
        ModelImportCandidate& candidate) {
        const auto handle = candidate.record.handle;
        const auto reject = [this, handle](const std::string& error) {
            invalidate_model(handle);
            LOG_ERROR("Failed to import model {}: {}", handle.value(), error);
            return ImportPublication::success(std::nullopt);
        };
        if(!candidate.result)
            return reject(candidate.result.error());
        if(auto published =
                m_import_service->publish_model(candidate.record, candidate.result.value());
            !published)
            return reject(published.error());
        const auto report = scan();
        if(!report.succeeded())
            return reject("Cannot index imported model products");
        m_loader->invalidate_model(handle);
        if(m_released_assets.contains(handle)) {
            publish_model_meshes(handle);
            m_source_imports_needing_recheck.erase(handle);
            return ImportPublication::success(handle);
        }
        auto loaded = m_loader->load_model(handle);
        if(!loaded)
            return reject(loaded.error().message);
        publish_model_meshes(handle);
        m_source_imports_needing_recheck.erase(handle);
        return ImportPublication::success(handle);
    }

    Result<void, Error> AssetManager::import_model(const AssetHandle handle) {
        if(m_database.is_read_only())
            return Result<void, Error>::failure({"Cannot import into a read-only product index"});
        const auto* record = find_asset_record(m_database, handle, AssetType::Model);
        if(!record)
            return Result<void, Error>::failure({"Model asset record not found"});
        const auto reject = [this, handle](Error error) {
            invalidate_model(handle);
            return Result<void, Error>::failure(std::move(error));
        };
        const AssetRecord snapshot = *record;
        auto prepared = m_import_service->prepare_model(snapshot, m_limits.mesh_working_bytes);
        // 成功会发布新的子资源身份；失败也接纳源变更，使旧资源不再冒充新版本。
        const auto report = scan();
        if(!prepared)
            return reject({prepared.error()});
        if(!report.succeeded())
            return reject({"Cannot index imported model products"});
        m_loader->invalidate_model(handle);
        const auto loaded = m_loader->load_model(handle);
        if(!loaded)
            return reject(loaded.error());
        publish_model_meshes(handle);
        return Result<void, Error>::success();
    }

    Result<std::shared_ptr<const ModelData>, Error> AssetManager::load_model(AssetHandle handle) {
        m_released_assets.erase(handle);
        return m_loader->load_model(handle);
    }

    Result<void, Error> AssetManager::import_mesh(const AssetHandle handle) {
        if(m_database.is_read_only())
            return Result<void, Error>::failure({"Cannot import into a read-only product index"});
        if(!validate_asset_handle(handle, "import a mesh")) {
            return Result<void, Error>::failure({"Invalid mesh handle"});
        }
        // 内置网格由 loader 直接加载，不生成项目索引或导入缓存。
        if(const auto* asset = find_default_asset(handle); asset && asset->type == AssetType::Mesh)
            return Result<void, Error>::success();

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Mesh);
        if(!record) {
            return Result<void, Error>::failure({"Mesh asset record not found"});
        }
        if(record->source_asset) {
            auto imported = import_model(record->source_asset);
            if(!imported)
                return imported;
            const auto* current = m_database.find(handle);
            if(!current || current->type != AssetType::Mesh)
                return Result<void, Error>::failure({"Model mesh no longer exists after import"});
            return Result<void, Error>::success();
        }
        const AssetRevision revision = m_database.get_revision(handle);
        const AssetRecord snapshot = *record;

        if(m_task_queue->contains(handle, revision)) {
            LOG_WARN("Mesh import is already running for handle {}", handle.value());
            return Result<void, Error>::failure({"Mesh import is already running"});
        }
        if(auto artifact = m_import_service->find_current_mesh_artifact(
               handle, snapshot.path, m_limits.mesh_working_bytes)) {
            complete_mesh_import(handle, revision, artifact->source_dependencies());
            LOG_DEBUG("Mesh artifact is current '{}' (handle {})", snapshot.path.generic_string(),
                handle.value());
            return Result<void, Error>::success();
        }

        auto imported = m_import_service->build_mesh_artifact(
            handle, snapshot.path, m_limits.mesh_working_bytes);
        if(!imported) {
            change_mesh_data(handle, false);
            LOG_ERROR("{}", imported.error());
            return Result<void, Error>::failure({imported.error()});
        }
        auto& artifact = imported.value();
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale mesh import for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<void, Error>::failure({"Mesh import revision is stale"});
        }

        if(auto result = artifact.publish_atomic(m_import_service->mesh_artifact_path(handle));
            !result) {
            LOG_ERROR("Failed to publish mesh artifact '{}' (handle {}): {}",
                snapshot.path.generic_string(), handle.value(), result.error());
            return Result<void, Error>::failure({result.error()});
        }
        complete_mesh_import(handle, revision, artifact.source_dependencies());

        if(auto refreshed = refresh_loaded_mesh(handle, revision, std::move(artifact.data));
            !refreshed)
            return Result<void, Error>::failure(refreshed.error());

        LOG_INFO("Imported mesh artifact '{}' (handle {})", snapshot.path.generic_string(),
            handle.value());
        return Result<void, Error>::success();
    }

    Result<void, Error> AssetManager::refresh_loaded_mesh(
        const AssetHandle handle, const AssetRevision revision, MeshData data) {
        const auto runtime =
            find_runtime_asset(m_registry, handle, m_registry.resolve<Mesh>(handle));
        if(runtime.type_conflict)
            return Result<void, Error>::failure({"Runtime mesh type conflict"});
        if(!runtime.asset)
            return Result<void, Error>::success();

        const auto publication = m_registry.get_revision(handle);
        auto candidate = m_render_factory->prepare(std::move(data));
        if(!candidate)
            return Result<void, Error>::failure(candidate.error());
        if(!m_database.is_current(handle, revision)
            || m_registry.get_revision(handle) != publication) {
            LOG_DEBUG("Discarded stale runtime mesh candidate for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<void, Error>::failure({"Runtime mesh revision is stale"});
        }
        if(!m_registry.replace_asset(handle, candidate.value()))
            return Result<void, Error>::failure({"Failed to publish runtime mesh"});
        return Result<void, Error>::success();
    }

    uint64_t AssetManager::mesh_data_revision(const AssetHandle handle) const {
        const auto found = m_mesh_data.find(handle);
        return found == m_mesh_data.end() ? 0 : found->second.revision;
    }

    void AssetManager::change_mesh_data(AssetHandle handle, bool available) {
        m_mesh_data[handle] = {++m_mesh_data_revision, available, false};
    }

    void AssetManager::publish_model_meshes(AssetHandle model) {
        for(const auto& record : m_database.get_assets())
            if(record.source_asset == model && record.type == AssetType::Mesh)
                change_mesh_data(record.handle, true);
    }

    Result<MeshData, Error> AssetManager::read_mesh_data(const AssetHandle handle) {
        auto& state = m_mesh_data[handle];
        state.prepared = false;
        if(!state.available)
            return Result<MeshData, Error>::failure(
                {"Mesh product is unavailable; import the changed asset before using its geometry"});
        auto data = m_loader->read_mesh_data(handle);
        state.prepared = bool(data);
        return data;
    }

    bool AssetManager::import_mesh_async(const AssetHandle handle, const AssetImportMode mode) {
        const auto* record = find_asset_record(m_database, handle, AssetType::Mesh);
        return record && schedule_mesh_task(*record, mode);
    }

    bool AssetManager::is_import_pending(const AssetHandle handle) const {
        const auto* record = m_database.find(handle);
        const auto source = record && record->source_asset ? record->source_asset : handle;
        return m_task_queue->contains(source, m_database.get_revision(source));
    }

    std::vector<AssetHandle> AssetManager::source_imports_needing_recheck() const {
        return {m_source_imports_needing_recheck.begin(), m_source_imports_needing_recheck.end()};
    }

    Result<std::shared_ptr<Mesh>, Error> AssetManager::load_mesh(const AssetHandle handle) {
        m_released_assets.erase(handle);
        if(const auto* record = m_database.find(handle); record && record->source_asset)
            m_released_assets.erase(record->source_asset);
        return m_loader->load_mesh(handle);
    }

    Result<std::shared_ptr<Texture>, Error> AssetManager::load_texture(const AssetHandle handle) {
        m_released_assets.erase(handle);
        if(const auto* record = m_database.find(handle); record && record->source_asset)
            m_released_assets.erase(record->source_asset);
        return m_loader->load_texture(handle);
    }

    Result<TextureData, Error> AssetManager::read_texture_data(AssetHandle handle) {
        return m_loader->read_texture_data(handle);
    }

    Result<TextureData, Error> AssetManager::read_environment_background(AssetHandle handle) {
        return m_loader->read_environment_background(handle);
    }

    Result<std::shared_ptr<ShaderProgramArtifact>, Error> AssetManager::load_shader_program(
        const AssetHandle handle) {
        m_released_assets.erase(handle);
        return m_loader->load_shader_program(handle);
    }

    Result<std::shared_ptr<AudioClip>, Error> AssetManager::load_audio(const AssetHandle handle) {
        return m_loader->load_audio(handle);
    }

    Result<std::shared_ptr<Environment>, Error> AssetManager::load_environment(
        const AssetHandle handle) {
        m_released_assets.erase(handle);
        const auto state = environment_state(handle);
        if(!state)
            return Result<std::shared_ptr<Environment>, Error>::failure(state.error());
        if(state.value() == EnvironmentState::Preparing)
            return Result<std::shared_ptr<Environment>, Error>::failure(
                {"Environment is still preparing; use request_load and references_ready"});
        if(state.value() == EnvironmentState::Failed)
            return Result<std::shared_ptr<Environment>, Error>::failure(
                {"Environment preparation failed; waiting for source changes"});
        return m_loader->load_environment(handle);
    }

    Result<std::shared_ptr<AudioClip>, Error> AssetManager::reimport_audio(
        const AssetHandle handle, const AudioImportSettings settings) {
        using Reimport = Result<std::shared_ptr<AudioClip>, Error>;
        const auto* record = m_database.find(handle);
        if(!record || record->type != AssetType::Audio || m_database.is_read_only())
            return Reimport::failure({"Audio import requires an editable audio asset"});
        if(m_registry.contains(handle) && !m_registry.resolve<AudioClip>(handle))
            return Reimport::failure({"Runtime audio type conflict"});
        auto path = m_database.paths().resolve_asset_path(record->path);
        if(!path)
            return Reimport::failure({path.error()});
        auto candidate = AudioClip::load(path.value(), settings.stream);
        if(!candidate)
            return candidate;
        if(auto saved = m_database.update_import_settings(handle, settings); !saved)
            return Reimport::failure({saved.error()});
        bool published;
        if(m_registry.contains(handle))
            published = m_registry.replace_asset(handle, candidate.value());
        else
            published = m_registry.register_asset(handle, candidate.value());
        if(!published)
            return Reimport::failure({"Cannot publish audio asset"});
        return candidate;
    }

    Result<std::shared_ptr<Texture>, Error> AssetManager::reimport_texture(
        const AssetHandle handle, TextureImportSettings import_settings) {
        if(m_database.is_read_only())
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Cannot import into a read-only product index"});
        if(!validate_asset_handle(handle, "reimport a texture")) {
            return Result<std::shared_ptr<Texture>, Error>::failure({"Invalid texture handle"});
        }

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Texture);
        if(!record) {
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Texture is not indexed with the expected type"});
        }

        if(record->source_asset)
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Texture settings are owned by the source model; reimport that model"});
        const auto runtime =
            find_runtime_asset(m_registry, handle, m_registry.resolve<Texture>(handle));
        if(runtime.type_conflict) {
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Runtime texture type conflict"});
        }
        const auto& previous_texture = runtime.asset;

        const AssetRevision revision = m_database.get_revision(handle);
        const AssetRecord snapshot = *record;
        const auto publication = m_registry.get_revision(handle);
        auto texture = m_loader->prepare_texture(snapshot, import_settings);
        if(!texture)
            return texture;
        if(!m_database.is_current(handle, revision)
            || m_registry.get_revision(handle) != publication) {
            LOG_DEBUG("Discarded stale texture reimport for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Texture changed during reimport"});
        }

        if(auto updated = m_database.update_import_settings(handle, import_settings); !updated) {
            return Result<std::shared_ptr<Texture>, Error>::failure({updated.error()});
        }

        bool published;
        if(previous_texture)
            published = m_registry.replace_asset(handle, texture.value());
        else
            published = m_registry.register_asset(handle, texture.value());
        if(!published) {
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Failed to publish runtime texture"});
        }

        if(auto refreshed = reload_loaded_material_dependents(handle); !refreshed)
            return Result<std::shared_ptr<Texture>, Error>::failure(refreshed.error());
        LOG_INFO("Reimported texture asset '{}' (handle {}, color_space={}, flip_y={})",
            snapshot.path.generic_string(), handle.value(), to_string(import_settings.color_space),
            import_settings.flip_y);
        return texture;
    }

    Result<void, Error> AssetManager::reload_loaded_material_dependents(
        const AssetHandle texture_handle) {
        const auto dependents = m_database.get_dependents(texture_handle);
        // 重载会修改依赖索引，先复制句柄。
        const std::vector<AssetHandle> snapshot(dependents.begin(), dependents.end());
        for(const auto handle : snapshot) {
            const auto* record = m_database.find(handle);
            if(!record || record->type != AssetType::Material
                || !m_registry.resolve<Material>(handle))
                continue;
            if(auto refreshed = reload_material(handle); !refreshed) {
                if(is_device_lost(refreshed.error()))
                    return Result<void, Error>::failure(refreshed.error());
                LOG_ERROR("Texture {} updated, but dependent material {} failed: {}",
                    texture_handle.value(), handle.value(), refreshed.error().message);
            }
        }
        return Result<void, Error>::success();
    }

    Result<std::shared_ptr<Material>, Error> AssetManager::load_material(const AssetHandle handle) {
        m_released_assets.erase(handle);
        if(const auto* record = m_database.find(handle); record && record->source_asset)
            m_released_assets.erase(record->source_asset);
        return m_loader->load_material(handle);
    }

    Result<std::shared_ptr<Material>, Error> AssetManager::reload_material(
        const AssetHandle handle) {
        if(m_database.is_read_only())
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Cannot reload a read-only product"});
        if(!validate_asset_handle(handle, "reload a material")) {
            return Result<std::shared_ptr<Material>, Error>::failure({"Invalid material handle"});
        }

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Material);
        if(!record) {
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Material is not indexed with the expected type"});
        }

        if(record->source_asset) {
            if(auto imported = import_model(record->source_asset); !imported)
                return Result<std::shared_ptr<Material>, Error>::failure(imported.error());
            static_cast<void>(m_registry.unregister_asset(handle));
            return load_material(handle);
        }

        const auto runtime =
            find_runtime_asset(m_registry, handle, m_registry.resolve<Material>(handle));
        if(runtime.type_conflict) {
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Runtime material type conflict"});
        }
        const bool has_runtime_asset = static_cast<bool>(runtime.asset);

        const AssetRevision revision = m_database.get_revision(handle);
        const auto publication = m_registry.get_revision(handle);
        const AssetRecord snapshot = *record;
        const auto data = MaterialSerializer{}.load(m_database.paths().assets() / snapshot.path);
        if(!data) {
            return Result<std::shared_ptr<Material>, Error>::failure({data.error()});
        }
        auto material = m_loader->prepare_material(snapshot, data.value());
        if(!material)
            return material;
        if(!m_database.is_current(handle, revision)
            || m_registry.get_revision(handle) != publication) {
            LOG_DEBUG("Discarded stale material reload for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Material changed during reload"});
        }
        if(auto published =
                publish_material(handle, data.value(), material.value(), has_runtime_asset);
            !published)
            return Result<std::shared_ptr<Material>, Error>::failure(published.error());
        LOG_INFO("Reloaded material asset '{}' (handle {})", snapshot.path.generic_string(),
            handle.value());
        return material;
    }

    std::shared_ptr<const Material> AssetManager::MaterialUpdate::material() const {
        return m_material;
    }

    Result<AssetManager::MaterialUpdate, Error> AssetManager::prepare_material_update(
        const AssetHandle handle, const MaterialData& data) {
        if(m_database.is_read_only())
            return Result<MaterialUpdate, Error>::failure({"Cannot edit a read-only product"});
        using Preparation = Result<MaterialUpdate, Error>;
        if(!validate_asset_handle(handle, "update a material")) {
            return Preparation::failure({"Invalid material handle"});
        }

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Material);
        if(!record) {
            return Preparation::failure({"Material is not indexed with the expected type"});
        }

        if(record->source_asset)
            return Preparation::failure(
                {"Imported material is owned by the source model; use a separate material asset"});

        const auto runtime =
            find_runtime_asset(m_registry, handle, m_registry.resolve<Material>(handle));
        if(runtime.type_conflict) {
            return Preparation::failure({"Runtime material type conflict"});
        }

        const AssetRevision revision = m_database.get_revision(handle);
        const AssetRecord snapshot = *record;
        if(auto valid = MaterialSerializer{}.validate(data); !valid)
            return Preparation::failure({valid.error()});
        const auto publication = m_registry.get_revision(handle);
        auto material = m_loader->prepare_material(snapshot, data);
        if(!material)
            return Preparation::failure(material.error());
        if(!m_database.is_current(handle, revision)
            || m_registry.get_revision(handle) != publication) {
            LOG_DEBUG("Discarded stale material update for asset handle {} (revision {})",
                handle.value(), revision);
            return Preparation::failure({"Material changed during update"});
        }
        MaterialUpdate update;
        update.m_owner = this;
        update.m_record = snapshot;
        update.m_revision = revision;
        update.m_publication = publication;
        update.m_data = data;
        update.m_material = std::move(material).value();
        update.m_expected = runtime.asset;
        return Preparation::success(std::move(update));
    }

    Result<std::shared_ptr<Material>, Error> AssetManager::commit_material_update(
        const MaterialUpdate& update) {
        if(m_database.is_read_only())
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Cannot edit a read-only product"});
        const auto handle = update.handle();
        if(!material_update_is_current(update))
            return Result<std::shared_ptr<Material>, Error>::failure({"Material update is stale"});
        const auto serialized = MaterialSerializer{}.serialize(update.m_data);
        if(!serialized)
            return Result<std::shared_ptr<Material>, Error>::failure({serialized.error()});
        if(auto saved = write_text_file_atomic(
               m_database.paths().assets() / update.m_record.path, serialized.value());
            !saved) {
            return Result<std::shared_ptr<Material>, Error>::failure({saved.error()});
        }
        if(auto published = publish_material(
               handle, update.m_data, update.m_material, static_cast<bool>(update.m_expected));
            !published)
            return Result<std::shared_ptr<Material>, Error>::failure(published.error());
        LOG_INFO("Updated material asset '{}' (handle {})", update.m_record.path.generic_string(),
            handle.value());
        return Result<std::shared_ptr<Material>, Error>::success(update.m_material);
    }

    Result<void, Error> AssetManager::preview_material_update(MaterialUpdate& update) {
        if(!material_update_is_current(update))
            return Result<void, Error>::failure({"Material preview is stale"});
        if(!m_registry.replace_asset(update.handle(), update.m_material))
            return Result<void, Error>::failure({"Cannot publish material preview"});
        update.m_expected = update.m_material;
        update.m_publication = m_registry.get_revision(update.handle());
        return Result<void, Error>::success();
    }

    Result<void, Error> AssetManager::restore_material_preview(
        const MaterialUpdate& update, const std::shared_ptr<Material>& previous) {
        if(!previous || !material_update_is_current(update)
            || update.m_expected != update.m_material)
            return Result<void, Error>::failure({"Material preview was replaced"});
        if(!m_registry.replace_asset(update.handle(), previous))
            return Result<void, Error>::failure({"Cannot restore material preview"});
        return Result<void, Error>::success();
    }

    bool AssetManager::material_update_is_current(const MaterialUpdate& update) const {
        return update.m_owner == this && update.m_material
               && m_database.is_current(update.handle(), update.m_revision)
               && m_registry.get_revision(update.handle()) == update.m_publication;
    }

    Result<void, Error> AssetManager::publish_material(const AssetHandle handle,
        const MaterialData& data, const std::shared_ptr<Material>& material,
        const bool replace_existing) {
        if(auto updated = m_database.update_dependencies(handle, get_asset_dependencies(data));
            !updated)
            return Result<void, Error>::failure({updated.error()});
        bool published;
        if(replace_existing)
            published = m_registry.replace_asset(handle, material);
        else
            published = m_registry.register_asset(handle, material);
        if(!published)
            return Result<void, Error>::failure({"Failed to publish material"});
        return Result<void, Error>::success();
    }

}
