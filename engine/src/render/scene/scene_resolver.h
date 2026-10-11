#pragma once

#include "asset/handle.h"
#include "common/export.h"
#include "render/scene/render_scene.h"
#include "render/scene/render_submission.h"

#include <optional>
#include <unordered_map>

namespace Comet {
    class AssetRegistry;

    class COMET_API SceneResolver {
    public:
        explicit SceneResolver(const AssetRegistry& asset_registry);

        [[nodiscard]] RenderWorld resolve(const RenderScene& render_scene);
        void resolve(const RenderScene& render_scene, RenderWorld& output);
        // 在帧边界释放已替换或移除的资源；新资源由 resolve 获取。
        void refresh_assets(RenderWorld& world) const;

        [[nodiscard]] std::optional<ViewProjectMatrix> resolve_camera(
            const RenderWorld& world, const RenderView& view);

    private:
        struct CameraDiagnostic {
            EntityId entity_id;
            RenderCamera::ProjectionIssue issue;

            bool operator==(const CameraDiagnostic&) const = default;
        };

        const AssetRegistry& m_asset_registry;
        std::unordered_map<AssetHandle, bool> m_missing_mesh_handles;
        std::unordered_map<AssetHandle, bool> m_missing_material_handles;
        AssetHandle m_invalid_environment;
        std::optional<CameraDiagnostic> m_camera_diagnostic;
        bool m_missing_primary_camera = false;
        bool m_missing_camera_override = false;
        bool m_multiple_primary_cameras = false;
        bool m_invalid_render_size = false;
    };
}
