#pragma once

#include "asset/handle.h"
#include "core/math_utils.h"
#include "scene/entity_id.h"

#include <vector>

namespace Comet {
    struct RenderItem {
        EntityId entity_id = INVALID_ENTITY_ID;
        Math::Mat4 model_matrix = Math::Mat4(1.0f);
        AssetHandle mesh_handle = INVALID_ASSET_HANDLE;
        AssetHandle material_handle = INVALID_ASSET_HANDLE;
    };

    struct RenderScene {
        std::vector<RenderItem> render_items;
    };
}
