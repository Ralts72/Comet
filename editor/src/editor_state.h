#pragma once

#include "render/scene/render_scene.h"

namespace CometEditor {
    enum class EditorMode { Edit, Play };

    struct EditorCameraState {
        Comet::Math::Vec3 target{0.0f, 0.0f, 0.0f};
        float near_clip = 0.1f;
        float far_clip = 1000.0f;
        Comet::RenderCamera::Projection projection =
            Comet::RenderCamera::Projection::Perspective;

        struct PerspectiveState {
            Comet::Math::Vec3 position{0.0f, 0.0f, 3.0f};
            Comet::Math::Vec3 up{0.0f, 1.0f, 0.0f};
            float fov_degrees = 45.0f;
        } perspective;

        struct OrthographicState {
            float height = 10.0f;
        } orthographic;

        [[nodiscard]] Comet::RenderCamera snapshot() const;
    };

    struct EditorState {
        EditorMode mode = EditorMode::Edit;
        EditorCameraState camera;
    };

    [[nodiscard]] Comet::RenderView make_render_view(
        const EditorState& state, bool visible, Comet::Math::Vec2u render_size);
}
