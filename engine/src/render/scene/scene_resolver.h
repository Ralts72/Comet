#pragma once

#include "asset/handle.h"
#include "common/export.h"
#include "render/scene/render_scene.h"
#include "render/scene/render_submission.h"

#include <optional>
#include <unordered_set>

namespace Comet {
    class AssetRegistry;

    class COMET_API SceneResolver {
    public:
        explicit SceneResolver(const AssetRegistry& asset_registry);

        [[nodiscard]] RenderSubmission resolve(
            const RenderScene& render_scene, const RenderView& view);

    private:
        [[nodiscard]] std::optional<ViewProjectMatrix> resolve_camera(
            const RenderScene& render_scene, const RenderView& view);

        [[nodiscard]] std::optional<ResolvedRenderItem> resolve_item(
            const RenderItem& render_item);

        const AssetRegistry& m_asset_registry;
        std::unordered_set<AssetHandle> m_missing_mesh_handles;
        std::unordered_set<AssetHandle> m_missing_material_handles;
        std::unordered_set<AssetHandle> m_invalid_material_handles;
        std::optional<EntityId> m_invalid_camera_fov;
        std::optional<EntityId> m_invalid_camera_clip_planes;
        bool m_missing_primary_camera = false;
        bool m_missing_camera_override = false;
        bool m_multiple_primary_cameras = false;
        bool m_invalid_render_size = false;
    };
}
