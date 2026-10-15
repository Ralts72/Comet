#pragma once

#include "asset/handle.h"
#include "common/export.h"
#include "render/scene/render_scene.h"
#include "render/scene/render_submission.h"

#include <optional>
#include <unordered_map>

namespace Comet {
    class AssetRegistry;

    // Main 解析 Handle 并固定材质／Shader 版本，GPU 准备只消费解析后的资源引用。
    class COMET_API SceneResolver {
    public:
        explicit SceneResolver(const AssetRegistry& asset_registry);

        [[nodiscard]] RenderWorld resolve(const RenderScene& render_scene);
        void resolve(const RenderScene& render_scene, RenderWorld& output);
        // 串行消费者复用存储；保留旧快照时，后续更新复制完整世界。
        [[nodiscard]] std::shared_ptr<const RenderWorld> update(const RenderScene& render_scene);
        void reset();
        void refresh_assets();
        // 在帧边界释放已替换或移除的资源；新资源由 resolve 获取。
        void refresh_assets(RenderWorld& world) const;

    private:
        RenderWorld& writable_output();
        void resolve_environment(const RenderScene& scene, RenderWorld& world);
        void resolve_particles(const RenderScene& scene, RenderWorld& world);
        [[nodiscard]] MaterialBinding resolve_material(
            AssetHandle handle, RenderWorld& world) const;
        const AssetRegistry& m_asset_registry;
        std::shared_ptr<RenderWorld> m_output;
        std::unordered_map<AssetHandle, bool> m_missing_mesh_handles;
        std::unordered_map<AssetHandle, bool> m_missing_material_handles;
        std::unordered_map<AssetHandle, bool> m_missing_particle_textures;
        AssetHandle m_invalid_environment;
    };

    // 每个 View 独立保留相机诊断，世界解析不消费视图状态。
    class COMET_API RenderCameraResolver {
    public:
        [[nodiscard]] std::optional<ViewProjectMatrix> resolve(
            const RenderWorld& world, const RenderView& view);

    private:
        struct CameraDiagnostic {
            EntityId entity_id;
            RenderCamera::ProjectionIssue issue;
            bool operator==(const CameraDiagnostic&) const = default;
        };
        std::optional<CameraDiagnostic> m_camera_diagnostic;
        bool m_missing_primary_camera = false;
        bool m_missing_camera_override = false;
        bool m_multiple_primary_cameras = false;
        bool m_invalid_render_size = false;
    };
}
