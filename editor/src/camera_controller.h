#pragma once

#include "editor_state.h"
#include "core/geometry.h"

#include <optional>

namespace CometEditor {
    struct EditorCameraInput {
        Comet::Math::Vec2 orbit_delta{};
        Comet::Math::Vec2 pan_delta{};
        float zoom_delta = 0.0f;
        float viewport_height = 0.0f;
    };

    void apply_editor_camera_input(
        EditorCameraState& camera, const EditorCameraInput& input);

    void focus_editor_camera(EditorCameraState& camera,
        const Comet::BoundingBox& world_bounds, float viewport_aspect);

    // UV 原点为图像左上角；放置平面经过 target，且平行于相机画面。
    [[nodiscard]] std::optional<Comet::Math::Vec3> camera_focus_plane_point(
        const EditorCameraState& camera, Comet::Math::Vec2 uv, float aspect);
}
