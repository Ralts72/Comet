#pragma once

#include "asset/handle.h"
#include "core/math_utils.h"
#include "render/scene/render_types.h"
#include "scene/entity_id.h"
#include "render/lighting.h"

#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    class Mesh;
    class Material;

    struct MaterialBinding {
        AssetHandle material_handle = INVALID_ASSET_HANDLE;
        std::shared_ptr<const Material> resource;
    };

    struct ResolvedRenderItem {
        EntityId entity_id = INVALID_ENTITY_ID;
        Math::Mat4 model_matrix = Math::Mat4(1.0f);
        std::shared_ptr<Mesh> mesh;
        MaterialBinding material;
    };

    struct RenderSubmission {
        std::optional<ViewProjectMatrix> view_project_matrix;
        std::vector<ResolvedRenderItem> render_items;
        std::vector<RenderLight> lights;
    };
}
