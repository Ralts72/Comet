#include "transform_gizmo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace CometEditor {
    namespace {
        constexpr float MIN_VALUE = 0.000001f;
        constexpr float MIN_PROJECTED_AXIS_LENGTH = 4.0f;

        bool is_finite(const Comet::Math::Vec2 value) {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        bool is_finite(const Comet::Math::Vec3 value) {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        bool is_finite(const Comet::Math::Vec4 value) {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z)
                && std::isfinite(value.w);
        }

        bool is_finite(const Comet::Math::Mat4& value) {
            for(int column = 0; column < 4; ++column) {
                if(!is_finite(value[column])) {
                    return false;
                }
            }
            return true;
        }

        std::optional<float> closest_axis_parameter(
            const Comet::Math::Vec3 axis_origin,
            const Comet::Math::Vec3 axis_direction,
            const Comet::Ray& ray) {
            if(!is_finite(axis_origin)
               || !is_finite(axis_direction)
               || !ray.is_valid()) {
                return std::nullopt;
            }

            const Comet::Math::Vec3 direction =
                Comet::Math::normalize(ray.direction);
            const Comet::Math::Vec3 offset = axis_origin - ray.origin;
            const float parallel = Comet::Math::dot(axis_direction, direction);
            const float denominator = 1.0f - parallel * parallel;
            if(!std::isfinite(denominator)
               || std::abs(denominator) <= MIN_VALUE) {
                return std::nullopt;
            }

            const float parameter = (
                parallel * Comet::Math::dot(direction, offset)
                - Comet::Math::dot(axis_direction, offset))
                / denominator;
            return std::isfinite(parameter)
                ? std::optional<float>(parameter)
                : std::nullopt;
        }

        float point_segment_distance_squared(
            const Comet::Math::Vec2 point,
            const Comet::Math::Vec2 start,
            const Comet::Math::Vec2 end) {
            const Comet::Math::Vec2 segment = end - start;
            const float length_squared =
                segment.x * segment.x + segment.y * segment.y;
            if(!std::isfinite(length_squared)
               || length_squared <= MIN_VALUE) {
                return std::numeric_limits<float>::infinity();
            }
            const float amount = std::clamp(
                ((point.x - start.x) * segment.x
                    + (point.y - start.y) * segment.y) / length_squared,
                0.0f,
                1.0f);
            const Comet::Math::Vec2 difference =
                point - (start + segment * amount);
            return difference.x * difference.x
                + difference.y * difference.y;
        }

        Comet::Math::Vec4 axis_color(
            const TransformGizmoAxis axis,
            const std::optional<TransformGizmoAxis> hovered_axis,
            const std::optional<TransformGizmoAxis> active_axis) {
            if(active_axis == axis || (!active_axis && hovered_axis == axis)) {
                return Comet::Math::Vec4(1.0f, 0.85f, 0.1f, 1.0f);
            }
            switch(axis) {
                case TransformGizmoAxis::X:
                    return Comet::Math::Vec4(0.95f, 0.2f, 0.18f, 1.0f);
                case TransformGizmoAxis::Y:
                    return Comet::Math::Vec4(0.2f, 0.9f, 0.3f, 1.0f);
                case TransformGizmoAxis::Z:
                    return Comet::Math::Vec4(0.2f, 0.45f, 1.0f, 1.0f);
            }
            return Comet::Math::Vec4(1.0f);
        }

        Comet::Math::Vec3 arrow_perpendicular(
            const TransformGizmoAxis axis) {
            return axis == TransformGizmoAxis::X
                ? Comet::Math::Vec3(0.0f, 1.0f, 0.0f)
                : Comet::Math::Vec3(1.0f, 0.0f, 0.0f);
        }
    }

    Comet::Math::Vec3 transform_gizmo_axis_direction(
        const TransformGizmoAxis axis) {
        switch(axis) {
            case TransformGizmoAxis::X:
                return Comet::Math::Vec3(1.0f, 0.0f, 0.0f);
            case TransformGizmoAxis::Y:
                return Comet::Math::Vec3(0.0f, 1.0f, 0.0f);
            case TransformGizmoAxis::Z:
                return Comet::Math::Vec3(0.0f, 0.0f, 1.0f);
        }
        return Comet::Math::Vec3(0.0f);
    }

    std::optional<TranslationGizmoFrame> make_translation_gizmo_frame(
        const EditorCameraState& camera,
        const Comet::Math::Vec3 world_origin,
        const Comet::Math::Vec2u render_resolution,
        const float axis_length_pixels) {
        if(!is_finite(world_origin)
           || render_resolution.x == 0
           || render_resolution.y == 0
           || !std::isfinite(axis_length_pixels)
           || axis_length_pixels <= 0.0f
           || !std::isfinite(camera.near_clip)
           || !std::isfinite(camera.far_clip)
           || camera.near_clip <= 0.0f
           || camera.far_clip <= camera.near_clip) {
            return std::nullopt;
        }

        const float aspect = static_cast<float>(render_resolution.x)
            / static_cast<float>(render_resolution.y);
        Comet::Math::Mat4 projection;
        float world_units_per_pixel = 0.0f;
        if(camera.projection == Comet::CameraProjection::Orthographic) {
            if(!std::isfinite(camera.orthographic_height)
               || camera.orthographic_height <= 0.0f) {
                return std::nullopt;
            }
            const float half_height = camera.orthographic_height * 0.5f;
            projection = Comet::Math::ortho(
                -half_height * aspect,
                half_height * aspect,
                -half_height,
                half_height,
                camera.near_clip,
                camera.far_clip);
            world_units_per_pixel = camera.orthographic_height
                / static_cast<float>(render_resolution.y);
        } else {
            if(!std::isfinite(camera.fov_degrees)
               || camera.fov_degrees <= 0.0f
               || camera.fov_degrees >= 180.0f
               || !is_finite(camera.position)
               || !is_finite(camera.target)) {
                return std::nullopt;
            }
            const Comet::Math::Vec3 forward_candidate =
                camera.target - camera.position;
            const float forward_length =
                Comet::Math::length(forward_candidate);
            if(!std::isfinite(forward_length)
               || forward_length <= MIN_VALUE) {
                return std::nullopt;
            }
            const Comet::Math::Vec3 forward =
                forward_candidate / forward_length;
            const float depth = Comet::Math::dot(
                world_origin - camera.position, forward);
            if(!std::isfinite(depth) || depth <= camera.near_clip) {
                return std::nullopt;
            }
            projection = Comet::Math::perspective(
                camera.fov_degrees,
                aspect,
                camera.near_clip,
                camera.far_clip);
            const float visible_world_height = 2.0f * depth * std::tan(
                Comet::Math::radians(camera.fov_degrees) * 0.5f);
            world_units_per_pixel = visible_world_height
                / static_cast<float>(render_resolution.y);
        }

        const float axis_length =
            world_units_per_pixel * axis_length_pixels;
        const Comet::RenderCamera snapshot = camera.snapshot();
        if(!std::isfinite(axis_length)
           || axis_length <= MIN_VALUE
           || !is_finite(snapshot.view_matrix)
           || !is_finite(projection)) {
            return std::nullopt;
        }
        return TranslationGizmoFrame{
            .origin = world_origin,
            .axis_length = axis_length,
            .view_project = {
                .view = snapshot.view_matrix,
                .projection = projection
            },
            .render_resolution = render_resolution
        };
    }

    std::optional<Comet::Math::Vec2> project_translation_gizmo_point(
        const TranslationGizmoFrame& frame,
        const Comet::Math::Vec3 world_point) {
        if(frame.render_resolution.x == 0
           || frame.render_resolution.y == 0
           || !is_finite(world_point)) {
            return std::nullopt;
        }
        const Comet::Math::Vec4 clip = frame.view_project.projection
            * frame.view_project.view
            * Comet::Math::Vec4(world_point, 1.0f);
        if(!is_finite(clip) || clip.w <= MIN_VALUE) {
            return std::nullopt;
        }
        const Comet::Math::Vec3 ndc = Comet::Math::Vec3(clip) / clip.w;
        if(!is_finite(ndc)) {
            return std::nullopt;
        }
        return Comet::Math::Vec2(
            (ndc.x + 1.0f) * 0.5f
                * static_cast<float>(frame.render_resolution.x),
            (ndc.y + 1.0f) * 0.5f
                * static_cast<float>(frame.render_resolution.y));
    }

    std::optional<Comet::Ray> make_translation_gizmo_ray(
        const TranslationGizmoFrame& frame,
        const Comet::Math::Vec2 pixel_position) {
        if(frame.render_resolution.x == 0
           || frame.render_resolution.y == 0
           || !is_finite(pixel_position)) {
            return std::nullopt;
        }
        const float ndc_x = pixel_position.x
            / static_cast<float>(frame.render_resolution.x) * 2.0f - 1.0f;
        const float ndc_y = pixel_position.y
            / static_cast<float>(frame.render_resolution.y) * 2.0f - 1.0f;
        const Comet::Math::Mat4 inverse_view_projection =
            Comet::Math::inverse(
                frame.view_project.projection * frame.view_project.view);
        if(!is_finite(inverse_view_projection)) {
            return std::nullopt;
        }
        Comet::Math::Vec4 near_point = inverse_view_projection
            * Comet::Math::Vec4(ndc_x, ndc_y, 0.0f, 1.0f);
        Comet::Math::Vec4 far_point = inverse_view_projection
            * Comet::Math::Vec4(ndc_x, ndc_y, 1.0f, 1.0f);
        if(!is_finite(near_point)
           || !is_finite(far_point)
           || std::abs(near_point.w) <= MIN_VALUE
           || std::abs(far_point.w) <= MIN_VALUE) {
            return std::nullopt;
        }
        near_point /= near_point.w;
        far_point /= far_point.w;
        const Comet::Math::Vec3 direction =
            Comet::Math::Vec3(far_point - near_point);
        const float direction_length = Comet::Math::length(direction);
        if(!std::isfinite(direction_length)
           || direction_length <= MIN_VALUE) {
            return std::nullopt;
        }
        return Comet::Ray{
            .origin = Comet::Math::Vec3(near_point),
            .direction = direction / direction_length
        };
    }

    std::optional<TransformGizmoAxis> pick_translation_gizmo_axis(
        const TranslationGizmoFrame& frame,
        const Comet::Math::Vec2 pixel_position,
        const float hit_radius_pixels) {
        if(!is_finite(pixel_position)
           || !std::isfinite(hit_radius_pixels)
           || hit_radius_pixels <= 0.0f) {
            return std::nullopt;
        }
        const std::optional<Comet::Math::Vec2> projected_origin =
            project_translation_gizmo_point(frame, frame.origin);
        if(!projected_origin) {
            return std::nullopt;
        }

        std::optional<TransformGizmoAxis> closest_axis;
        float closest_distance_squared = hit_radius_pixels
            * hit_radius_pixels;
        constexpr std::array axes{
            TransformGizmoAxis::X,
            TransformGizmoAxis::Y,
            TransformGizmoAxis::Z
        };
        for(const TransformGizmoAxis axis: axes) {
            const std::optional<Comet::Math::Vec2> projected_end =
                project_translation_gizmo_point(
                    frame,
                    frame.origin + transform_gizmo_axis_direction(axis)
                        * frame.axis_length);
            const Comet::Math::Vec2 projected_axis = projected_end
                ? *projected_end - *projected_origin
                : Comet::Math::Vec2(0.0f);
            const float projected_axis_length = std::sqrt(
                projected_axis.x * projected_axis.x
                + projected_axis.y * projected_axis.y);
            if(!projected_end
               || projected_axis_length < MIN_PROJECTED_AXIS_LENGTH) {
                continue;
            }
            const float distance_squared = point_segment_distance_squared(
                pixel_position, *projected_origin, *projected_end);
            if(distance_squared <= closest_distance_squared
               && (!closest_axis
                   || distance_squared < closest_distance_squared)) {
                closest_distance_squared = distance_squared;
                closest_axis = axis;
            }
        }
        return closest_axis;
    }

    std::optional<TranslationGizmoDrag> begin_translation_gizmo_drag(
        const TranslationGizmoFrame& frame,
        const TransformGizmoAxis axis,
        const Comet::Math::Vec2 pixel_position,
        const Comet::Math::Mat4& world_to_parent) {
        if(!is_finite(world_to_parent)) {
            return std::nullopt;
        }
        const std::optional<Comet::Ray> ray = make_translation_gizmo_ray(
            frame, pixel_position);
        const std::optional<float> parameter = ray
            ? closest_axis_parameter(
                frame.origin,
                transform_gizmo_axis_direction(axis),
                *ray)
            : std::nullopt;
        if(!parameter) {
            return std::nullopt;
        }
        const Comet::Math::Vec4 local_origin = world_to_parent
            * Comet::Math::Vec4(frame.origin, 1.0f);
        if(!is_finite(local_origin)
           || std::abs(local_origin.w) <= MIN_VALUE) {
            return std::nullopt;
        }
        return TranslationGizmoDrag{
            .axis = axis,
            .world_origin = frame.origin,
            .world_to_parent = world_to_parent,
            .start_axis_parameter = *parameter
        };
    }

    std::optional<Comet::Math::Vec3> update_translation_gizmo_drag(
        const TranslationGizmoDrag& drag,
        const Comet::Ray& pointer_ray) {
        const Comet::Math::Vec3 axis =
            transform_gizmo_axis_direction(drag.axis);
        const std::optional<float> parameter = closest_axis_parameter(
            drag.world_origin, axis, pointer_ray);
        if(!parameter) {
            return std::nullopt;
        }
        const Comet::Math::Vec3 desired_world_origin = drag.world_origin
            + axis * (*parameter - drag.start_axis_parameter);
        const Comet::Math::Vec4 local = drag.world_to_parent
            * Comet::Math::Vec4(desired_world_origin, 1.0f);
        if(!is_finite(local) || std::abs(local.w) <= MIN_VALUE) {
            return std::nullopt;
        }
        const Comet::Math::Vec3 translation = Comet::Math::Vec3(local)
            / local.w;
        return is_finite(translation)
            ? std::optional<Comet::Math::Vec3>(translation)
            : std::nullopt;
    }

    bool add_translation_gizmo(
        Comet::DebugDrawList& draw_list,
        const TranslationGizmoFrame& frame,
        const std::optional<TransformGizmoAxis> hovered_axis,
        const std::optional<TransformGizmoAxis> active_axis) {
        if(!is_finite(frame.origin)
           || !std::isfinite(frame.axis_length)
           || frame.axis_length <= 0.0f) {
            return false;
        }
        constexpr std::array axes{
            TransformGizmoAxis::X,
            TransformGizmoAxis::Y,
            TransformGizmoAxis::Z
        };
        bool added = true;
        for(const TransformGizmoAxis axis: axes) {
            const Comet::Math::Vec3 direction =
                transform_gizmo_axis_direction(axis);
            const Comet::Math::Vec3 end =
                frame.origin + direction * frame.axis_length;
            const Comet::Math::Vec3 arrow_base = end
                - direction * frame.axis_length * 0.18f;
            const Comet::Math::Vec3 perpendicular =
                arrow_perpendicular(axis) * frame.axis_length * 0.08f;
            const Comet::Math::Vec4 color = axis_color(
                axis, hovered_axis, active_axis);
            added = draw_list.add_line(frame.origin, end, color) && added;
            added = draw_list.add_line(
                end, arrow_base + perpendicular, color) && added;
            added = draw_list.add_line(
                end, arrow_base - perpendicular, color) && added;
        }
        return added;
    }

    TranslationGizmoUpdate TranslationGizmoController::update(
        const std::optional<TranslationGizmoContext>& context,
        const std::optional<TranslationGizmoPointerInput>& input) {
        TranslationGizmoUpdate result;
        if(!context || !context->entity_uuid) {
            result.edit = cancel();
            return result;
        }
        if(m_active && m_active->entity_uuid != context->entity_uuid) {
            result.edit = cancel();
            return result;
        }
        if(!input) {
            if(!m_active) {
                m_hovered_axis.reset();
            }
            return result;
        }

        if(m_active) {
            const TransformGizmoAxis axis = m_active->drag.axis;
            m_hovered_axis = axis;
            result.pointer_press_consumed = true;
            Comet::Math::Vec3 after = context->local_translation;
            if(input->down || input->released) {
                const std::optional<Comet::Ray> ray =
                    make_translation_gizmo_ray(
                        context->frame, input->pixel_position);
                const std::optional<Comet::Math::Vec3> translation = ray
                    ? update_translation_gizmo_drag(m_active->drag, *ray)
                    : std::nullopt;
                if(translation) {
                    after = *translation;
                    result.edit = TranslationGizmoEdit{
                        .entity_uuid = m_active->entity_uuid,
                        .translation = after
                    };
                }
            }
            if(input->released) {
                result.commit = TranslationGizmoCommit{
                    .entity_uuid = m_active->entity_uuid,
                    .before = m_active->before,
                    .after = after
                };
                m_active.reset();
                m_hovered_axis = axis;
            }
            return result;
        }

        m_hovered_axis = input->pointer_over_image
            ? pick_translation_gizmo_axis(
                context->frame,
                input->pixel_position,
                context->hit_radius_pixels)
            : std::nullopt;
        if(!input->pressed
           || !input->pointer_over_image
           || !m_hovered_axis) {
            return result;
        }

        const auto drag = begin_translation_gizmo_drag(
            context->frame,
            *m_hovered_axis,
            input->pixel_position,
            context->world_to_parent);
        if(!drag) {
            return result;
        }
        m_active = ActiveDrag{
            .entity_uuid = context->entity_uuid,
            .before = context->local_translation,
            .drag = *drag
        };
        result.pointer_press_consumed = true;
        return result;
    }

    std::optional<TranslationGizmoEdit>
    TranslationGizmoController::cancel() {
        if(!m_active) {
            m_hovered_axis.reset();
            return std::nullopt;
        }
        const TranslationGizmoEdit restore{
            .entity_uuid = m_active->entity_uuid,
            .translation = m_active->before
        };
        reset();
        return restore;
    }

    void TranslationGizmoController::reset() {
        m_active.reset();
        m_hovered_axis.reset();
    }
}
