#pragma once

#include "render/scene/render_scene.h"

namespace CometEditor {
    enum class EditorMode { Edit, Play };

    struct EditorCameraState {
        Comet::Math::Vec3 position{0.0f, 0.0f, 3.0f};
        Comet::Math::Vec3 target{0.0f, 0.0f, 0.0f};
        Comet::Math::Vec3 up{0.0f, 1.0f, 0.0f};
        float fov_degrees = 45.0f;
        float near_clip = 0.1f;
        float far_clip = 1000.0f;

        [[nodiscard]] Comet::RenderCamera snapshot() const {
            return {.view_matrix = Comet::Math::look_at(position, target, up),
                .fov_degrees = fov_degrees,
                .near_clip = near_clip,
                .far_clip = far_clip};
        }
    };

    struct EditorState {
        EditorMode mode = EditorMode::Edit;
        EditorCameraState camera;
    };

    [[nodiscard]] inline Comet::RenderView make_render_view(const EditorState& state,
        const bool visible, const Comet::Math::Vec2u render_size) {
        Comet::RenderView view{.visible = visible,
            .render_size = render_size,
            .camera_selection = state.mode == EditorMode::Edit
                                    ? Comet::RenderView::CameraSelection::Override
                                    : Comet::RenderView::CameraSelection::ScenePrimary};
        if(state.mode == EditorMode::Edit) {
            view.camera_override = state.camera.snapshot();
        }
        return view;
    }
}
