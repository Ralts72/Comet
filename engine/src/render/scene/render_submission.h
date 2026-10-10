#pragma once

#include "asset/handle.h"
#include "core/math_utils.h"
#include "render/scene/render_types.h"
#include "render/scene/render_scene.h"
#include "scene/entity_id.h"
#include "render/lighting.h"
#include "scene/scene_settings.h"

#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    class Mesh;
    class Material;
    struct MaterialOverrides;
    struct Environment;

    struct MaterialBinding {
        AssetHandle material_handle = INVALID_ASSET_HANDLE;
        std::shared_ptr<const Material> resource;
        std::shared_ptr<const MaterialOverrides> overrides;
        uint64_t asset_revision = 0;
    };

    struct ResolvedRenderItem {
        EntityId entity_id = INVALID_ENTITY_ID;
        uint64_t transform_revision = 0;
        Math::Mat4 model_matrix = Math::Mat4(1.0f);
        AssetHandle mesh_handle = INVALID_ASSET_HANDLE;
        uint64_t mesh_revision = 0;
        std::shared_ptr<Mesh> mesh;
        MaterialBinding material;
    };

    struct RenderSubmission {
        uint64_t scene_lifetime = 0;
        uint64_t asset_revision = 0;
        std::optional<ViewProjectMatrix> view_project_matrix;
        std::vector<ResolvedRenderItem> render_items;
        std::vector<RenderLight> lights;
        SceneEnvironment environment;
        PostProcessSettings post_process;
        uint64_t environment_revision = 0;
        std::shared_ptr<Environment> environment_resource;
        uint64_t scene_revision = 0;
        RenderItemChanges item_changes;
    };
}
