#include "viewport/camera_controller.h"

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

    namespace {
        constexpr float ORBIT_RADIANS_PER_PIXEL = 0.005f;
        constexpr float ZOOM_EXPONENT_PER_STEP = 0.15f;
        constexpr float MIN_DISTANCE = 0.05f;
        constexpr float MAX_DISTANCE = 10000.0f;
        constexpr float MIN_ORTHOGRAPHIC_HEIGHT = 0.01f;
        constexpr float MAX_ORTHOGRAPHIC_HEIGHT = 10000.0f;
        constexpr float FOCUS_PADDING = 1.2f;
        constexpr float MAX_VERTICAL_ALIGNMENT = 0.995f;
        constexpr float MIN_DIRECTION_LENGTH = 0.00001f;

        bool has_delta(const Comet::Math::Vec2 value) {
            return value.x != 0.0f || value.y != 0.0f;
        }
    }

    std::optional<Comet::Math::Vec3> camera_focus_plane_point(
        const EditorCameraState& camera, const Comet::Math::Vec2 uv, const float aspect) {
        namespace Math = Comet::Math;
        if(!Math::is_finite(uv) || uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1
            || !std::isfinite(aspect) || aspect <= 0 || !Math::is_finite(camera.target))
            return std::nullopt;
        const auto snapshot = camera.snapshot();
        const auto projection = snapshot.projection_matrix(aspect);
        if(!projection)
            return std::nullopt;
        const auto view_target = snapshot.view_matrix * Math::Vec4(camera.target, 1);
        const float depth = -view_target.z;
        if(!std::isfinite(depth) || depth < camera.near_clip || depth > camera.far_clip)
            return std::nullopt;
        float height = 2.0f / (*projection)[1][1];
        if(camera.projection == Comet::RenderCamera::Projection::Perspective)
            height *= depth;
        if(!std::isfinite(height) || height <= 0)
            return std::nullopt;
        const auto offset =
            Math::inverse(snapshot.view_matrix)
            * Math::Vec4((uv.x - 0.5f) * height * aspect, (0.5f - uv.y) * height, 0, 0);
        const auto point = camera.target + Math::Vec3(offset);
        if(!Math::is_finite(point))
            return std::nullopt;
        return point;
    }

    void focus_editor_camera(EditorCameraState& camera,
        const Comet::BoundingBox& world_bounds, const float viewport_aspect) {
        if(!world_bounds.is_valid() || !std::isfinite(viewport_aspect)
            || viewport_aspect <= 0.0f) {
            return;
        }

        const Comet::Math::Vec3 offset = camera.perspective.position - camera.target;
        const float previous_distance = Comet::Math::length(offset);
        const Comet::Math::Vec3 center = world_bounds.center();
        const Comet::Math::Vec3 size = world_bounds.size();
        if(!std::isfinite(previous_distance) || previous_distance < MIN_DIRECTION_LENGTH
            || !Comet::Math::is_finite(center) || !Comet::Math::is_finite(size)) {
            return;
        }

        if(camera.projection == Comet::RenderCamera::Projection::Orthographic) {
            const double required_height =
                std::max(static_cast<double>(size.y),
                    static_cast<double>(size.x) / viewport_aspect)
                * FOCUS_PADDING;
            const Comet::Math::Vec3 position = center + offset;
            if(!Comet::Math::is_finite(position)) {
                return;
            }
            camera.orthographic.height = static_cast<float>(
                std::clamp(required_height, static_cast<double>(MIN_ORTHOGRAPHIC_HEIGHT),
                    static_cast<double>(MAX_ORTHOGRAPHIC_HEIGHT)));
            camera.target = center;
            camera.perspective.position = position;
            return;
        }

        if(!std::isfinite(camera.perspective.fov_degrees)
            || camera.perspective.fov_degrees <= 0.0f
            || camera.perspective.fov_degrees >= 179.0f) {
            return;
        }
        const double vertical_half_fov =
            Comet::Math::radians(static_cast<double>(camera.perspective.fov_degrees))
            * 0.5;
        const double horizontal_half_fov =
            std::atan(std::tan(vertical_half_fov) * viewport_aspect);
        const double radius =
            std::hypot(static_cast<double>(size.x), static_cast<double>(size.y),
                static_cast<double>(size.z))
            * 0.5;
        const double required_distance =
            radius * FOCUS_PADDING
            / std::sin(std::min(vertical_half_fov, horizontal_half_fov));
        const float distance = static_cast<float>(std::clamp(required_distance,
            static_cast<double>(MIN_DISTANCE), static_cast<double>(MAX_DISTANCE)));
        const Comet::Math::Vec3 position = center + offset / previous_distance * distance;
        if(!Comet::Math::is_finite(position)) {
            return;
        }
        camera.target = center;
        camera.perspective.position = position;
    }

    void apply_editor_camera_input(
        EditorCameraState& camera, const EditorCameraInput& input) {
        if(!Comet::Math::is_finite(input.orbit_delta)
            || !Comet::Math::is_finite(input.pan_delta)
            || !std::isfinite(input.zoom_delta)) {
            return;
        }

        Comet::Math::Vec3 offset = camera.perspective.position - camera.target;
        float distance = Comet::Math::length(offset);
        if(!std::isfinite(distance) || distance < MIN_DIRECTION_LENGTH) {
            return;
        }

        const Comet::Math::Vec3 world_up(0.0f, 1.0f, 0.0f);
        const bool orthographic =
            camera.projection == Comet::RenderCamera::Projection::Orthographic;

        if(!orthographic && has_delta(input.orbit_delta)) {
            offset = Comet::Math::angle_axis(
                         -input.orbit_delta.x * ORBIT_RADIANS_PER_PIXEL, world_up)
                     * offset;

            const Comet::Math::Vec3 forward = -Comet::Math::normalize(offset);
            const Comet::Math::Vec3 right_candidate =
                Comet::Math::cross(forward, world_up);
            if(Comet::Math::length(right_candidate) >= MIN_DIRECTION_LENGTH) {
                const Comet::Math::Vec3 right = Comet::Math::normalize(right_candidate);
                const Comet::Math::Vec3 pitched =
                    Comet::Math::angle_axis(
                        -input.orbit_delta.y * ORBIT_RADIANS_PER_PIXEL, right)
                    * offset;
                const Comet::Math::Vec3 pitched_forward =
                    -Comet::Math::normalize(pitched);
                if(std::abs(Comet::Math::dot(pitched_forward, world_up))
                    < MAX_VERTICAL_ALIGNMENT) {
                    offset = pitched;
                }
            }
            camera.perspective.position = camera.target + offset;
            camera.perspective.up = world_up;
            distance = Comet::Math::length(offset);
        }

        const bool valid_orthographic_pan_scale =
            orthographic && std::isfinite(camera.orthographic.height)
            && camera.orthographic.height > 0.0f;
        const bool valid_perspective_pan_scale =
            !orthographic && std::isfinite(camera.perspective.fov_degrees)
            && camera.perspective.fov_degrees > 0.0f
            && camera.perspective.fov_degrees < 179.0f;
        const bool valid_pan_scale =
            valid_orthographic_pan_scale || valid_perspective_pan_scale;
        if(has_delta(input.pan_delta) && std::isfinite(input.viewport_height)
            && input.viewport_height > 0.0f && valid_pan_scale) {
            Comet::Math::Vec3 forward;
            float visible_world_height = 0.0f;
            if(orthographic) {
                forward = Comet::Math::Vec3(0.0f, 0.0f, -1.0f);
                visible_world_height = camera.orthographic.height;
            } else {
                forward =
                    Comet::Math::normalize(camera.target - camera.perspective.position);
                visible_world_height =
                    2.0f * distance
                    * std::tan(
                        Comet::Math::radians(camera.perspective.fov_degrees) * 0.5f);
            }
            const Comet::Math::Vec3 right_candidate =
                Comet::Math::cross(forward, world_up);
            if(Comet::Math::length(right_candidate) >= MIN_DIRECTION_LENGTH) {
                const Comet::Math::Vec3 right = Comet::Math::normalize(right_candidate);
                const Comet::Math::Vec3 camera_up =
                    Comet::Math::normalize(Comet::Math::cross(right, forward));
                const float world_units_per_pixel =
                    visible_world_height / input.viewport_height;
                const Comet::Math::Vec3 translation =
                    right * (-input.pan_delta.x * world_units_per_pixel)
                    + camera_up * (input.pan_delta.y * world_units_per_pixel);
                camera.perspective.position += translation;
                camera.target += translation;
            }
        }

        if(input.zoom_delta != 0.0f) {
            if(orthographic) {
                if(!std::isfinite(camera.orthographic.height)
                    || camera.orthographic.height <= 0.0f) {
                    return;
                }
                camera.orthographic.height =
                    std::clamp(camera.orthographic.height
                                   * std::exp(-input.zoom_delta * ZOOM_EXPONENT_PER_STEP),
                        MIN_ORTHOGRAPHIC_HEIGHT, MAX_ORTHOGRAPHIC_HEIGHT);
                return;
            }

            const Comet::Math::Vec3 direction =
                Comet::Math::normalize(camera.perspective.position - camera.target);
            distance = std::clamp(
                distance * std::exp(-input.zoom_delta * ZOOM_EXPONENT_PER_STEP),
                MIN_DISTANCE, MAX_DISTANCE);
            camera.perspective.position = camera.target + direction * distance;
        }
    }
}
