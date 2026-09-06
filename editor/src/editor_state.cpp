#include "editor_state.h"

#include <algorithm>
#include <cmath>

namespace CometEditor {
    namespace {
        constexpr float FALLBACK_MIN_VIEW_DISTANCE = 0.05f;
        constexpr float FALLBACK_MAX_VIEW_DISTANCE = 1000.0f;
        constexpr float CLIP_RANGE_MARGIN_RATIO = 0.001f;
    }

    Comet::RenderCamera EditorCameraState::snapshot() const {
        const float camera_distance = Comet::Math::length(perspective.position - target);
        const bool valid_clip_range = std::isfinite(near_clip) && std::isfinite(far_clip)
                                      && near_clip > 0.0f && far_clip > near_clip;
        const float clip_margin =
            valid_clip_range ? (far_clip - near_clip) * CLIP_RANGE_MARGIN_RATIO : 0.0f;
        const float minimum_view_distance =
            valid_clip_range ? near_clip + clip_margin : FALLBACK_MIN_VIEW_DISTANCE;
        const float maximum_view_distance =
            valid_clip_range ? far_clip - clip_margin : FALLBACK_MAX_VIEW_DISTANCE;
        const float view_distance = std::clamp(
            std::isfinite(camera_distance) ? camera_distance : minimum_view_distance,
            minimum_view_distance, maximum_view_distance);
        const bool use_orthographic_projection =
            projection == Comet::RenderCamera::Projection::Orthographic;
        Comet::Math::Vec3 view_position = perspective.position;
        Comet::Math::Vec3 view_up = perspective.up;
        if(use_orthographic_projection) {
            view_position = target + Comet::Math::Vec3(0.0f, 0.0f, view_distance);
            view_up = Comet::Math::Vec3(0.0f, 1.0f, 0.0f);
        }

        return {
            .view_matrix = Comet::Math::look_at(view_position, target, view_up),
            .projection = projection,
            .fov_degrees = perspective.fov_degrees,
            .orthographic_height = orthographic.height,
            .near_clip = near_clip,
            .far_clip = far_clip,
        };
    }

    Comet::RenderView make_render_view(const EditorState& state, const bool visible,
        const Comet::Math::Vec2u render_size) {
        Comet::RenderView view{.visible = visible, .render_size = render_size};
        if(state.mode == EditorMode::Edit) {
            view.camera_selection = Comet::RenderView::CameraSelection::Override;
            view.camera_override = state.camera.snapshot();
        }
        return view;
    }
}
