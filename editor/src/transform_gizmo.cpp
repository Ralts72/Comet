#include "transform_gizmo.h"

#include <algorithm>
#include <cmath>

namespace CometEditor {
    namespace {
        constexpr float EPSILON = 0.000001f;
        constexpr float AXIS_LENGTH = 90.0f;
        constexpr float HIT_RADIUS = 7.0f;
        constexpr float MIN_PROJECTED_LENGTH = 6.0f;
        constexpr int ROTATION_SEGMENTS = 64;

        bool uniform_orthogonal(const Comet::Math::Mat3& matrix) {
            const float length = glm::length(matrix[0]);
            if(!std::isfinite(length) || length <= EPSILON)
                return false;
            const auto normalized = matrix / length;
            for(int i = 0; i < 3; ++i) {
                if(std::abs(glm::length(normalized[i]) - 1) > 0.0001f)
                    return false;
                for(int j = i + 1; j < 3; ++j)
                    if(std::abs(glm::dot(normalized[i], normalized[j])) > 0.0001f)
                        return false;
            }
            return true;
        }

        bool finite_matrix(const Comet::Math::Mat4& matrix) {
            for(int column = 0; column < 4; ++column) {
                if(!Comet::Math::is_finite(matrix[column]))
                    return false;
            }
            return true;
        }

        Comet::Math::Vec3 axis_direction(const TransformGizmo::Axis axis) {
            Comet::Math::Vec3 result(0.0f);
            result[static_cast<int>(axis)] = 1.0f;
            return result;
        }

        bool same_layout(const ViewportLayout& left, const ViewportLayout& right) {
            return left.panel_content_size == right.panel_content_size
                   && left.render_resolution == right.render_resolution
                   && left.image_resolution == right.image_resolution
                   && left.image_display_rect.min == right.image_display_rect.min
                   && left.image_display_rect.max == right.image_display_rect.max
                   && left.image_visible_rect.min == right.image_visible_rect.min
                   && left.image_visible_rect.max == right.image_visible_rect.max;
        }

        std::optional<Comet::Math::Vec2> project(const Comet::Math::Mat4& view_projection,
            const ViewportLayout& layout, const Comet::Math::Vec3 point) {
            const auto clip = view_projection * Comet::Math::Vec4(point, 1.0f);
            if(!Comet::Math::is_finite(clip) || clip.w <= EPSILON)
                return std::nullopt;
            const auto ndc = Comet::Math::Vec3(clip) / clip.w;
            if(!Comet::Math::is_finite(ndc) || ndc.z < 0.0f || ndc.z > 1.0f)
                return std::nullopt;
            // 与场景渲染的负高度 Viewport 一致，界面 Y 轴向下。
            const auto normalized = Comet::Math::Vec2(ndc.x + 1.0f, 1.0f - ndc.y) * 0.5f;
            const auto screen = layout.image_display_rect.min
                                + normalized * layout.image_display_rect.size();
            if(!Comet::Math::is_finite(screen))
                return std::nullopt;
            return screen;
        }

        float distance_squared(
            const TransformGizmo::Segment& handle, const Comet::Math::Vec2 point) {
            const auto segment = handle.end - handle.start;
            const float length_squared = glm::dot(segment, segment);
            const float t = std::clamp(
                glm::dot(point - handle.start, segment) / length_squared, 0.0f, 1.0f);
            const auto distance = point - (handle.start + segment * t);
            return glm::dot(distance, distance);
        }
    }

    TransformGizmo::TransformGizmo(
        CommandHistory& history, const Comet::ComponentRegistry& registry)
        : m_history(history), m_edit(history, registry) {}

    bool TransformGizmo::set_settings(const Settings settings) {
        if(!std::isfinite(settings.translation_step) || settings.translation_step <= 0
            || !std::isfinite(settings.rotation_step_degrees)
            || settings.rotation_step_degrees <= 0 || !std::isfinite(settings.scale_step)
            || settings.scale_step <= 0
            || (settings.mode != Mode::Translate && settings.mode != Mode::Rotate
                && settings.mode != Mode::Scale)
            || (settings.space != Space::World && settings.space != Space::Local))
            return false;
        if(settings == m_settings)
            return true;
        if(!cancel())
            return false;
        m_settings = settings;
        return true;
    }

    std::optional<TransformGizmo::Context> TransformGizmo::make_context(
        const Comet::EntityUuid selected, const Comet::RenderCamera& camera,
        const ViewportLayout& layout) const {
        auto* scene = m_history.get_scene();
        const auto display_size = layout.image_display_rect.size();
        if(!scene || !selected || layout.image_resolution.x == 0
            || layout.image_resolution.y == 0
            || !Comet::Math::is_finite(layout.image_display_rect.min)
            || !Comet::Math::is_finite(display_size) || display_size.x <= 0.0f
            || display_size.y <= 0.0f || !finite_matrix(camera.view_matrix)
            || !std::isfinite(camera.near_clip) || !std::isfinite(camera.far_clip)
            || camera.near_clip <= 0.0f || camera.far_clip <= camera.near_clip)
            return std::nullopt;
        const auto entity = scene->find_entity(selected);
        if(!entity || !entity.has_component<Comet::TransformComponent>())
            return std::nullopt;

        Context context;
        context.layout = layout;
        context.translation =
            entity.get_component<Comet::TransformComponent>().translation;
        context.rotation = entity.get_component<Comet::TransformComponent>().rotation;
        context.scale = entity.get_component<Comet::TransformComponent>().scale;
        const auto world_origin =
            scene->get_world_matrix(entity) * Comet::Math::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        if(!Comet::Math::is_finite(world_origin) || std::abs(world_origin.w) <= EPSILON)
            return std::nullopt;
        context.origin = Comet::Math::Vec3(world_origin) / world_origin.w;
        if(const auto parent = scene->get_parent(entity)) {
            context.parent = parent.get_uuid();
            context.parent_world = scene->get_world_matrix(parent);
            context.world_to_parent = Comet::Math::inverse(context.parent_world);
        }
        if(!finite_matrix(context.parent_world)
            || !finite_matrix(context.world_to_parent))
            return std::nullopt;

        const auto local_rotation =
            Comet::Math::compose_trs({}, context.rotation, Comet::Math::Vec3(1));
        for(std::size_t index = 0; index < context.directions.size(); ++index) {
            auto direction = axis_direction(static_cast<Axis>(index));
            if(m_settings.space == Space::Local || m_settings.mode == Mode::Scale) {
                // 不让实体自身的零／负缩放反转手柄；父级仿射变换仍影响本地轴。
                direction = Comet::Math::Vec3(context.parent_world * local_rotation
                                              * Comet::Math::Vec4(direction, 0));
            }
            const float length = Comet::Math::length(direction);
            if(!Comet::Math::is_finite(direction) || !std::isfinite(length)
                || length <= EPSILON)
                return std::nullopt;
            context.directions[index] = direction / length;
        }
        if(m_settings.mode == Mode::Rotate) {
            if(m_settings.space == Space::World) {
                if(!uniform_orthogonal(Comet::Math::Mat3(context.parent_world)))
                    return std::nullopt;
            } else {
                context.rotation_frame =
                    Comet::Math::Mat3(context.parent_world * local_rotation);
                const float largest = std::max({glm::length(context.rotation_frame[0]),
                    glm::length(context.rotation_frame[1]),
                    glm::length(context.rotation_frame[2])});
                context.rotation_frame /= largest;
                context.inverse_rotation_frame = glm::inverse(context.rotation_frame);
                if(!finite_matrix(Comet::Math::Mat4(context.inverse_rotation_frame)))
                    return std::nullopt;
            }
        }

        const float aspect = static_cast<float>(layout.image_resolution.x)
                             / static_cast<float>(layout.image_resolution.y);
        Comet::Math::Mat4 projection;
        float visible_height;
        if(camera.projection == Comet::RenderCamera::Projection::Perspective) {
            if(!std::isfinite(camera.fov_degrees) || camera.fov_degrees <= 0.0f
                || camera.fov_degrees >= 180.0f)
                return std::nullopt;
            projection = Comet::Math::perspective(
                camera.fov_degrees, aspect, camera.near_clip, camera.far_clip);
            const auto view_origin =
                camera.view_matrix * Comet::Math::Vec4(context.origin, 1.0f);
            visible_height = -2.0f * view_origin.z / projection[1][1];
        } else {
            if(!std::isfinite(camera.orthographic_height)
                || camera.orthographic_height <= 0.0f)
                return std::nullopt;
            const float half_height = camera.orthographic_height * 0.5f;
            projection = Comet::Math::ortho(-half_height * aspect, half_height * aspect,
                -half_height, half_height, camera.near_clip, camera.far_clip);
            visible_height = camera.orthographic_height;
        }
        context.view_projection = projection * camera.view_matrix;
        context.inverse_view_projection = Comet::Math::inverse(context.view_projection);
        context.axis_length = visible_height * AXIS_LENGTH / display_size.y;
        if(!finite_matrix(context.view_projection)
            || !finite_matrix(context.inverse_view_projection)
            || !std::isfinite(context.axis_length))
            return std::nullopt;
        return context;
    }

    std::array<std::optional<TransformGizmo::Handle>, 4> TransformGizmo::make_handles(
        const Context& context) const {
        std::array<std::optional<Handle>, 4> result{};
        // 裁剪只影响显示和开始命中；已开始的拖动不因自身移动而取消。
        if(context.axis_length <= EPSILON)
            return result;
        const auto start =
            project(context.view_projection, context.layout, context.origin);
        if(!start)
            return result;
        for(std::size_t index = 0; index < context.directions.size(); ++index) {
            const auto axis = static_cast<Axis>(index);
            if(m_settings.mode == Mode::Rotate) {
                Handle handle{.axis = axis};
                const auto point = [&](const int segment) {
                    const float angle = 2 * Comet::Math::PI * segment / ROTATION_SEGMENTS;
                    Comet::Math::Vec3 local(0);
                    local[(index + 1) % 3] = std::cos(angle);
                    local[(index + 2) % 3] = std::sin(angle);
                    return project(context.view_projection, context.layout,
                        context.origin
                            + context.rotation_frame * local * context.axis_length
                                  * 0.8f);
                };
                auto previous = point(0);
                for(int segment = 1; segment <= ROTATION_SEGMENTS; ++segment) {
                    const auto next = point(segment);
                    if(previous && next && glm::length(*next - *previous) > EPSILON
                        && rotation_parameter(context, axis, (*previous + *next) * 0.5f))
                        handle.segments.push_back({*previous, *next});
                    previous = next;
                }
                if(!handle.segments.empty())
                    result[index] = std::move(handle);
                continue;
            }
            const auto end = project(context.view_projection, context.layout,
                context.origin + context.directions[index] * context.axis_length);
            if(end && glm::length(*end - *start) >= MIN_PROJECTED_LENGTH)
                result[index] = Handle{axis, {{*start, *end}}};
        }
        if(m_settings.mode == Mode::Scale)
            result[3] = Handle{Axis::All,
                {{*start - Comet::Math::Vec2(3, 0), *start + Comet::Math::Vec2(3, 0)}}};
        return result;
    }

    std::array<std::optional<TransformGizmo::Handle>, 4> TransformGizmo::handles(
        const Comet::EntityUuid selected, const Comet::RenderCamera& camera,
        const ViewportLayout& layout) const {
        const auto context = make_context(selected, camera, layout);
        if(!context)
            return {};
        if(m_settings.mode == Mode::Rotate && active() && selected == m_drag->entity
            && m_history.generation() == m_drag->generation)
            return make_handles(m_drag->context);
        return make_handles(*context);
    }

    std::optional<Comet::Ray> TransformGizmo::pointer_ray(
        const Context& context, const Comet::Math::Vec2 position) {
        if(!Comet::Math::is_finite(position))
            return std::nullopt;
        const auto normalized = (position - context.layout.image_display_rect.min)
                                / context.layout.image_display_rect.size();
        const auto ndc =
            Comet::Math::Vec2(normalized.x * 2.0f - 1.0f, 1.0f - normalized.y * 2.0f);
        auto near_point =
            context.inverse_view_projection * Comet::Math::Vec4(ndc, 0.0f, 1.0f);
        auto far_point =
            context.inverse_view_projection * Comet::Math::Vec4(ndc, 1.0f, 1.0f);
        if(!Comet::Math::is_finite(near_point) || !Comet::Math::is_finite(far_point)
            || std::abs(near_point.w) <= EPSILON || std::abs(far_point.w) <= EPSILON)
            return std::nullopt;
        near_point /= near_point.w;
        far_point /= far_point.w;
        const auto delta = Comet::Math::Vec3(far_point - near_point);
        const float length = Comet::Math::length(delta);
        if(!std::isfinite(length) || length <= EPSILON)
            return std::nullopt;
        return Comet::Ray{Comet::Math::Vec3(near_point), delta / length};
    }

    std::optional<float> TransformGizmo::axis_parameter(
        const Context& context, const Axis axis, const Comet::Math::Vec2 position) {
        const auto ray = pointer_ray(context, position);
        if(!ray)
            return std::nullopt;
        const auto ray_direction = ray->direction;
        const auto direction = context.directions[static_cast<std::size_t>(axis)];
        const auto offset = context.origin - ray->origin;
        const float parallel = Comet::Math::dot(direction, ray_direction);
        const float denominator = 1.0f - parallel * parallel;
        if(!std::isfinite(denominator) || denominator <= EPSILON)
            return std::nullopt;
        const float parameter = (parallel * Comet::Math::dot(ray_direction, offset)
                                    - Comet::Math::dot(direction, offset))
                                / denominator;
        if(!std::isfinite(parameter))
            return std::nullopt;
        return parameter;
    }

    std::optional<float> TransformGizmo::rotation_parameter(
        const Context& context, const Axis axis, const Comet::Math::Vec2 position) {
        const auto ray = pointer_ray(context, position);
        if(!ray)
            return std::nullopt;
        const auto origin =
            context.inverse_rotation_frame * (ray->origin - context.origin);
        const auto direction = context.inverse_rotation_frame * ray->direction;
        const auto index = static_cast<std::size_t>(axis);
        if(!Comet::Math::is_finite(direction) || std::abs(direction[index]) <= EPSILON)
            return std::nullopt;
        const float parameter = -origin[index] / direction[index];
        if(!std::isfinite(parameter) || parameter < 0)
            return std::nullopt;
        const auto point = origin + direction * parameter;
        const float u = point[(index + 1) % 3];
        const float v = point[(index + 2) % 3];
        if(!Comet::Math::is_finite(point) || std::hypot(u, v) <= EPSILON)
            return std::nullopt;
        return std::atan2(v, u);
    }

    std::optional<float> TransformGizmo::parameter(
        const Context& context, const Axis axis, const Comet::Math::Vec2 position) const {
        if(axis == Axis::All) {
            if(m_settings.mode != Mode::Scale || !Comet::Math::is_finite(position))
                return std::nullopt;
            // 中心手柄沿屏幕右上方向缩放，不依赖某个投影后可能退化的三维轴。
            return glm::dot(position, glm::normalize(Comet::Math::Vec2(1, -1)))
                   * context.axis_length / AXIS_LENGTH;
        }
        if(m_settings.mode == Mode::Rotate)
            return rotation_parameter(context, axis, position);
        return axis_parameter(context, axis, position);
    }

    Comet::Math::Vec3 TransformGizmo::preview_value(
        Drag& drag, const float parameter) const {
        namespace Math = Comet::Math;
        if(m_settings.mode == Mode::Scale) {
            float delta = (parameter - drag.start_parameter) / drag.context.axis_length;
            if(m_settings.snap)
                delta = std::round(delta / m_settings.scale_step) * m_settings.scale_step;
            if(drag.axis == Axis::All)
                return drag.context.scale * (1 + delta);
            auto scale = drag.context.scale;
            scale[static_cast<int>(drag.axis)] += delta;
            return scale;
        }
        if(m_settings.mode == Mode::Translate) {
            float distance = parameter - drag.start_parameter;
            if(m_settings.snap)
                distance = std::round(distance / m_settings.translation_step)
                           * m_settings.translation_step;
            const auto world_delta =
                drag.context.directions[static_cast<std::size_t>(drag.axis)] * distance;
            const auto local_delta =
                drag.context.world_to_parent * Math::Vec4(world_delta, 0);
            return drag.context.translation + Math::Vec3(local_delta);
        }
        drag.accumulated_angle +=
            std::remainder(parameter - drag.last_parameter, 2 * Math::PI);
        drag.last_parameter = parameter;
        float degrees = drag.accumulated_angle * Math::RAD_TO_DEG;
        if(m_settings.snap)
            degrees = std::round(degrees / m_settings.rotation_step_degrees)
                      * m_settings.rotation_step_degrees;
        degrees = Math::wrap_degrees(degrees);
        if(std::abs(degrees) < 0.0001f)
            return drag.context.rotation;
        const auto start =
            Math::Mat3(Math::compose_trs({}, drag.context.rotation, Math::Vec3(1)));
        const auto delta = Math::Mat3(Math::rotate(
            Math::Mat4(1), Math::radians(degrees), axis_direction(drag.axis)));
        Math::Mat3 rotation;
        if(m_settings.space == Space::Local)
            rotation = start * delta;
        else
            rotation = Math::Mat3(drag.context.world_to_parent) * delta
                       * Math::Mat3(drag.context.parent_world) * start;
        const auto quaternion = glm::normalize(glm::quat_cast(rotation));
        return Math::wrap_degrees(glm::eulerAngles(quaternion) * Math::RAD_TO_DEG);
    }

    bool TransformGizmo::update(const Comet::EntityUuid selected,
        const Comet::RenderCamera& camera, const ViewportLayout& layout,
        const Input& input) {
        m_hovered_axis.reset();
        const auto context = make_context(selected, camera, layout);
        if(m_drag) {
            if(input.cancel || !context || selected != m_drag->entity
                || m_history.generation() != m_drag->generation
                || context->parent != m_drag->context.parent
                || context->parent_world != m_drag->context.parent_world
                || (m_settings.mode == Mode::Translate
                    && context->directions != m_drag->context.directions)
                || (m_settings.mode == Mode::Rotate
                    && (context->origin != m_drag->context.origin
                        || context->rotation != m_drag->last_value))
                || (m_settings.mode == Mode::Scale
                    && (context->origin != m_drag->context.origin
                        || context->rotation != m_drag->context.rotation
                        || context->scale != m_drag->last_value))
                || context->view_projection != m_drag->context.view_projection
                || !same_layout(context->layout, m_drag->context.layout)
                || (!input.down && !input.released)) {
                static_cast<void>(cancel());
                return true;
            }
            const auto current_parameter =
                parameter(m_drag->context, m_drag->axis, input.position);
            if(!current_parameter) {
                static_cast<void>(cancel());
                return true;
            }
            const auto value = preview_value(*m_drag, *current_parameter);
            if(!Comet::Math::is_finite(value) || !m_edit.preview(value)) {
                static_cast<void>(cancel());
                return true;
            }
            m_drag->last_value = value;
            if(input.released) {
                if(!m_edit.commit())
                    static_cast<void>(m_edit.cancel());
                m_drag.reset();
            }
            return true;
        }

        if(input.cancel || !context || !input.hovered
            || !layout.image_visible_rect.contains(input.position)
            || !Comet::Math::is_finite(input.position))
            return false;
        float nearest = HIT_RADIUS * HIT_RADIUS;
        for(const auto& handle : make_handles(*context)) {
            if(!handle)
                continue;
            for(const auto& segment : handle->segments) {
                const float distance = distance_squared(segment, input.position);
                if(distance < nearest
                    || (handle->axis == Axis::All
                        && distance < HIT_RADIUS * HIT_RADIUS)) {
                    nearest = distance;
                    m_hovered_axis = handle->axis;
                }
            }
        }
        if(!input.pressed || !input.down || !m_hovered_axis)
            return false;
        const auto start_parameter = parameter(*context, *m_hovered_axis, input.position);
        const char* property = "translation";
        auto initial = context->translation;
        if(m_settings.mode == Mode::Rotate) {
            property = "rotation";
            initial = context->rotation;
        } else if(m_settings.mode == Mode::Scale) {
            property = "scale";
            initial = context->scale;
        }
        if(!start_parameter || !m_edit.begin({selected, "transform", property}))
            return false;
        m_drag = Drag{selected, m_history.generation(), *m_hovered_axis, *context,
            *start_parameter, *start_parameter, 0, initial};
        return true;
    }

    bool TransformGizmo::cancel() {
        m_drag.reset();
        m_hovered_axis.reset();
        return m_edit.cancel();
    }

    bool TransformGizmo::active() const {
        return m_drag.has_value() && m_edit.active();
    }

    std::optional<TransformGizmo::Axis> TransformGizmo::active_axis() const {
        if(!active())
            return std::nullopt;
        return m_drag->axis;
    }
}
