#pragma once

#include "asset/handle.h"
#include "common/shader_resources.h"
#include "core/math_utils.h"
#include "scene/entity_id.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    class Mesh;
    class Texture;

    struct MaterialBinding {
        AssetHandle material_handle = INVALID_ASSET_HANDLE;
        std::array<std::shared_ptr<Texture>, 2> textures;
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
    };
}
