#include "assets/reflection_bake.h"

#include "assets/editor_assets.h"
#include "asset/import/environment_importer.h"
#include "common/binary.h"
#include "render/renderer.h"
#include "render/scene/scene_preview.h"
#include "render/scene/scene_resolver.h"
#include "scene/command_history.h"
#include "scene/scene.h"

#include <algorithm>
#include <bit>

namespace CometEditor {
    using namespace Comet;
    namespace {
        class BakeCommand final: public CommandHistory::Command {
        public:
            BakeCommand(
                EntityUuid entity, ReflectionProbeComponent before, ReflectionProbeComponent after)
                : m_entity(entity), m_before(std::move(before)), m_after(std::move(after)) {}
            bool undo(Scene& scene) override { return apply(scene, m_before); }
            bool redo(Scene& scene) override { return apply(scene, m_after); }

        private:
            bool apply(Scene& scene, const ReflectionProbeComponent& value) const {
                auto entity = scene.find_entity(m_entity);
                return entity && entity.try_set_component(value);
            }
            EntityUuid m_entity;
            ReflectionProbeComponent m_before;
            ReflectionProbeComponent m_after;
        };
    }

    static Result<ReflectionBake::Input> describe_reflection_input(Scene& scene, EntityUuid uuid,
        const AssetDatabase& assets, uint64_t shader_fingerprint,
        LightingBakeInput::SourceCache* source_cache = nullptr) {
        using Outcome = Result<ReflectionBake::Input>;
        auto entity = scene.find_entity(uuid);
        if(!entity || !entity.has_component<ReflectionProbeComponent>())
            return Outcome::failure("反射探针已不存在");
        const auto& probe = entity.get_component<ReflectionProbeComponent>();
        if(probe.capture_resolution < 16 || probe.capture_resolution > 512
            || !std::has_single_bit(probe.capture_resolution))
            return Outcome::failure("烘焙面尺寸须为 16、32、64、128、256 或 512");
        if(!std::isfinite(probe.capture_distance) || probe.capture_distance <= 0.05f
            || probe.capture_distance > 100000)
            return Outcome::failure("烘焙距离须大于 0.05 米且不超过 100000 米");
        auto common = LightingBakeInput::describe(scene, assets,
            LightingBakeInput::Purpose::Reflection, shader_fingerprint, source_cache);
        if(!common)
            return Outcome::failure(common.error());
        ReflectionBake::Input input{.scene = std::move(common.value().scene),
            .position = Math::Vec3(scene.get_world_matrix(entity)[3]),
            .resolution = probe.capture_resolution,
            .distance = probe.capture_distance,
            .dependencies = std::move(common.value().dependencies)};
        if(!Math::is_finite(input.position))
            return Outcome::failure("反射探针位置必须为有限值");
        Binary::Writer writer;
        writer.write_string(common.value().fingerprint, 64);
        writer.write_u32(EnvironmentImporter::VERSION);
        writer.write_u32(input.resolution);
        writer.write_float(input.distance);
        for(int axis = 0; axis < 3; ++axis)
            writer.write_float(input.position[axis]);
        input.fingerprint = std::to_string(Binary::hash_bytes(writer.data()));
        return Outcome::success(std::move(input));
    }

    Result<ReflectionBake::Input> ReflectionBake::describe_input(
        Scene& scene, EntityUuid entity, const AssetDatabase& assets, uint64_t shader_fingerprint) {
        return describe_reflection_input(scene, entity, assets, shader_fingerprint);
    }

    ReflectionBake::ReflectionBake(
        Renderer& renderer, EditorAssets& assets, const AssetRegistry& registry)
        : m_renderer(renderer), m_assets(assets), m_registry(registry) {}
    ReflectionBake::~ReflectionBake() = default;

    Result<void> ReflectionBake::start(
        Scene& scene, EntityUuid entity, const CommandHistory& history) {
        if(busy())
            return Result<void>::failure("已有反射探针正在烘焙");
        if(history.get_scene() != &scene)
            return Result<void>::failure("只能烘焙当前编辑场景");
        const auto input = describe_input(scene, entity, m_assets.database(),
            m_renderer.get_material_program_feedback()->builtin_fingerprint);
        if(!input)
            return Result<void>::failure(input.error());
        SceneResolver resolver(m_registry);
        auto world = resolver.update(input.value().scene);
        if((world->environment.background || world->environment.lighting)
            && world->environment.asset && !world->environment_resource)
            return Result<void>::failure("烘焙环境资产尚未就绪，请等待导入完成");
        if(world->render_items.size() != input.value().scene.render_items.size())
            return Result<void>::failure("部分烘焙资源尚未就绪，请等待导入完成");
        for(const auto& item : world->render_items)
            if(!item.mesh || !item.material.resource)
                return Result<void>::failure("部分烘焙网格或材质尚未就绪");
        auto capture =
            std::make_unique<ScenePreview>(m_renderer, Math::Vec2u(input.value().resolution));
        auto submitted =
            capture->submit_cubemap(world, input.value().position, 0.05f, input.value().distance);
        if(!submitted)
            return Result<void>::failure(submitted.error().message);
        m_entity = entity;
        m_lifetime = scene.get_lifetime();
        m_generation = history.generation();
        m_versions.clear();
        for(const auto handle : input.value().dependencies)
            m_versions.emplace_back(handle, m_registry.get_revision(handle));
        m_fingerprint = input.value().fingerprint;
        m_capture = std::move(capture);
        return Result<void>::success();
    }

    Result<bool> ReflectionBake::update(Scene& scene, CommandHistory& history) {
        if(!m_capture)
            return Result<bool>::success(false);
        if(scene.get_lifetime() != m_lifetime || history.generation() != m_generation) {
            cancel();
            return Result<bool>::success(false);
        }
        auto pixels = m_capture->poll();
        if(!pixels) {
            cancel();
            return Result<bool>::failure(pixels.error().message);
        }
        if(!pixels.value())
            return Result<bool>::success(false);
        m_capture.reset();
        const auto current = describe_input(scene, m_entity, m_assets.database(),
            m_renderer.get_material_program_feedback()->builtin_fingerprint);
        if(!current || current.value().fingerprint != m_fingerprint
            || std::ranges::any_of(m_versions, [&](const auto& version) {
                   return m_registry.get_revision(version.first) != version.second;
               }))
            return Result<bool>::failure("烘焙期间场景或资源已变化，请重新烘焙");
        const auto path =
            std::filesystem::path("baked")
            / ("reflection_" + m_entity.to_string() + "_" + Uuid::generate().to_string() + ".hdr");
        const auto report = m_assets.create_environment(path, *pixels.value());
        if(!report.succeeded())
            return Result<bool>::failure(report.issues.front().message);
        const auto* record = m_assets.database().find(path);
        if(!record)
            return Result<bool>::failure("烘焙环境未能加入资产索引");
        auto entity = scene.find_entity(m_entity);
        const auto before = entity.get_component<ReflectionProbeComponent>();
        auto after = before;
        after.environment = record->handle;
        after.rotation = 0;
        after.bake_fingerprint = m_fingerprint + ":" + std::to_string(record->handle.value());
        if(!history.execute(std::make_unique<BakeCommand>(m_entity, before, std::move(after))))
            return Result<bool>::failure("无法记录反射烘焙撤销命令");
        m_status.clear();
        return Result<bool>::success(true);
    }

    std::string ReflectionBake::status(Scene& scene, EntityUuid entity) {
        if(busy() && m_entity == entity)
            return "正在捕获六个方向…";
        const auto shader = m_renderer.get_material_program_feedback()->builtin_fingerprint;
        const LightingBakeInput::StatusKey key{entity, scene.get_lifetime(),
            scene.get_render_revision(), m_assets.database().generation(),
            m_registry.get_revision(), shader};
        if(const auto* cached = m_status.find(key))
            return *cached;
        auto found = scene.find_entity(entity);
        if(!found || !found.has_component<ReflectionProbeComponent>())
            return {};
        const auto& probe = found.get_component<ReflectionProbeComponent>();
        std::string text;
        if(probe.bake_fingerprint.empty())
            text = "尚未烘焙；也可直接选择已有环境贴图";
        else {
            auto input = describe_reflection_input(scene, entity, m_assets.database(), shader,
                &m_status.sources(m_assets.database().generation()));
            if(!input)
                text = input.error();
            else if(input.value().fingerprint + ":" + std::to_string(probe.environment.value())
                    != probe.bake_fingerprint)
                text = "烘焙已过期：几何、材质、光照、捕获设置或贴图引用发生变化";
            else if(!m_assets.database().find(probe.environment))
                text = "烘焙环境资产缺失，请重新烘焙";
            else
                text = "烘焙输入未变化";
        }
        m_status.store(key, text);
        return text;
    }

    void ReflectionBake::cancel() {
        m_capture.reset();
        m_entity = {};
        m_versions.clear();
        m_status.clear();
    }
}
