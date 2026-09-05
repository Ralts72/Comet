#pragma once

#include "editor_state.h"

namespace CometEditor {
    struct EditorCameraInput {
        Comet::Math::Vec2 orbit_delta{};
        Comet::Math::Vec2 pan_delta{};
        float zoom_delta = 0.0f;
        float viewport_height = 0.0f;
    };

    void apply_editor_camera_input(
        EditorCameraState& camera, const EditorCameraInput& input);
}
