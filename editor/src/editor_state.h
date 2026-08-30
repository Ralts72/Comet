#pragma once

#include "render/scene/render_scene.h"

#include <algorithm>
#include <cmath>

namespace CometEditor {
    enum class EditorMode {
        Edit,
        Play
    };

    struct EditorCameraState {
        Comet::Math::Vec3 position{0.0f, 0.0f, 3.0f};
        Comet::Math::Vec3 target{0.0f, 0.0f, 0.0f};
        Comet::Math::Vec3 up{0.0f, 1.0f, 0.0f};
        Comet::CameraProjection projection =
            Comet::CameraProjection::Perspective;
        float fov_degrees = 45.0f;
        float orthographic_height = 10.0f;
        float near_clip = 0.1f;
        float far_clip = 1000.0f;

        [[nodiscard]] Comet::RenderCamera snapshot() const {
            const float distance =
                Comet::Math::length(position - target);
            const bool valid_clip_range =
                std::isfinite(near_clip)
                && std::isfinite(far_clip)
                && near_clip > 0.0f
                && far_clip > near_clip;
            const float clip_margin = valid_clip_range
                ? (far_clip - near_clip) * 0.001f
                : 0.0f;
            const float minimum_view_distance = valid_clip_range
                ? near_clip + clip_margin
                : 0.05f;
            const float maximum_view_distance = valid_clip_range
                ? far_clip - clip_margin
                : 1000.0f;
            const float view_distance = std::clamp(
                std::isfinite(distance)
                    ? distance
                    : minimum_view_distance,
                minimum_view_distance,
                maximum_view_distance);
            const Comet::Math::Vec3 view_position =
                projection == Comet::CameraProjection::Orthographic
                ? target + Comet::Math::Vec3(
                    0.0f,
                    0.0f,
                    view_distance)
                : position;
            return {
                .view_matrix = Comet::Math::look_at(
                    view_position,
                    target,
                    projection == Comet::CameraProjection::Orthographic
                        ? Comet::Math::Vec3(0.0f, 1.0f, 0.0f)
                        : up),
                .projection = projection,
                .fov_degrees = fov_degrees,
                .orthographic_height = orthographic_height,
                .near_clip = near_clip,
                .far_clip = far_clip
            };
        }
    };

    struct EditorState {
        EditorMode mode = EditorMode::Edit;
        EditorCameraState camera;
    };

    [[nodiscard]] inline Comet::ViewportRenderRequest
    make_viewport_render_request(
        const EditorState& state,
        const bool visible,
        const Comet::Math::Vec2u render_size) {
        Comet::ViewportRenderRequest request{
            .visible = visible,
            .render_size = render_size,
            .camera_source = state.mode == EditorMode::Edit
                ? Comet::ViewportCameraSource::Explicit
                : Comet::ViewportCameraSource::ScenePrimary
        };
        if(state.mode == EditorMode::Edit) {
            request.explicit_camera = state.camera.snapshot();
        }
        return request;
    }
}
