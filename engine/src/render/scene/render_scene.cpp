#include "render/scene/render_scene.h"

#include <cmath>

namespace Comet {
    std::optional<RenderCamera::ProjectionIssue> RenderCamera::projection_issue(
        const float aspect) const {
        if(projection == Projection::Perspective) {
            if(!std::isfinite(fov_degrees) || fov_degrees <= 0 || fov_degrees >= 180)
                return ProjectionIssue::InvalidFov;
        } else if(!std::isfinite(orthographic_height) || orthographic_height <= 0) {
            return ProjectionIssue::InvalidOrthographicHeight;
        }
        if(!std::isfinite(near_clip) || !std::isfinite(far_clip) || near_clip <= 0
            || far_clip <= near_clip)
            return ProjectionIssue::InvalidClipPlanes;
        if(!std::isfinite(aspect) || aspect <= 0)
            return ProjectionIssue::InvalidAspect;
        for(int i = 0; i < 4; ++i) {
            if(!Math::is_finite(view_matrix[i]))
                return ProjectionIssue::InvalidView;
        }
        return std::nullopt;
    }

    std::optional<Math::Mat4> RenderCamera::projection_matrix(const float aspect) const {
        if(projection_issue(aspect))
            return std::nullopt;
        if(projection == Projection::Perspective)
            return Math::perspective(fov_degrees, aspect, near_clip, far_clip);
        const float half_height = orthographic_height * 0.5f;
        const float half_width = half_height * aspect;
        return Math::ortho(
            -half_width, half_width, -half_height, half_height, near_clip, far_clip);
    }
}
