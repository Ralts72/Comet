#pragma once

#include "asset/handle.h"
#include "common/export.h"
#include "render_scene.h"
#include "render_submission.h"

#include <optional>
#include <unordered_set>

namespace Comet {
    class AssetRegistry;

    class COMET_API RenderSceneResolver {
    public:
        explicit RenderSceneResolver(const AssetRegistry& asset_registry);

        [[nodiscard]] RenderSubmission resolve(const RenderScene& render_scene);

    private:
        [[nodiscard]] std::optional<ResolvedRenderItem> resolve_item(
            const RenderItem& render_item);

        const AssetRegistry& m_asset_registry;
        std::unordered_set<AssetHandle> m_missing_mesh_handles;
        std::unordered_set<AssetHandle> m_missing_material_handles;
        std::unordered_set<AssetHandle> m_invalid_material_handles;
    };
}
