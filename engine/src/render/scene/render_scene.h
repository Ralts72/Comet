#pragma once

#include "asset/handle.h"
#include "core/math_utils.h"
#include "scene/entity_id.h"
#include "render/lighting.h"

#include <optional>
#include <vector>

namespace Comet {
    struct RenderCamera {
        enum class Projection { Perspective, Orthographic };

        EntityId entity_id = INVALID_ENTITY_ID;
        bool primary = false;
        Math::Mat4 view_matrix = Math::Mat4(1.0f);
        Projection projection = Projection::Perspective;
        float fov_degrees = 45.0f;
        float orthographic_height = 10.0f;
        float near_clip = 0.1f;
        float far_clip = 1000.0f;
    };

    struct RenderItem {
        EntityId entity_id = INVALID_ENTITY_ID;
        Math::Mat4 model_matrix = Math::Mat4(1.0f);
        AssetHandle mesh_handle = INVALID_ASSET_HANDLE;
        AssetHandle material_handle = INVALID_ASSET_HANDLE;
    };

    struct RenderScene {
        std::vector<RenderCamera> cameras;
        std::vector<RenderItem> render_items;
        std::vector<RenderLight> lights;
    };

    struct RenderView {
        enum class CameraSelection { ScenePrimary, Override };

        bool visible = true;
        Math::Vec2u render_size{};
        CameraSelection camera_selection = CameraSelection::ScenePrimary;
        std::optional<RenderCamera> camera_override;
    };
}
