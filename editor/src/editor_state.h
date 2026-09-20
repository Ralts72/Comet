#pragma once

#include "viewport/camera_controller.h"

namespace CometEditor {
    enum class EditorMode { Edit, Play };
    enum class PlayCommand { Play, Stop, Pause, Resume, Step };

    struct EditorState {
        EditorMode mode = EditorMode::Edit;
        EditorCameraState camera;
    };

    [[nodiscard]] Comet::RenderView make_render_view(
        const EditorState& state, bool visible, Comet::Math::Vec2u render_size);
}
