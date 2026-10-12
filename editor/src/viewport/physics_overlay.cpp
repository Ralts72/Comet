#include "viewport/physics_overlay.h"
#include "render/debug/line_draw_list.h"
#include "scene/scene.h"

namespace CometEditor {
    namespace {
        constexpr Comet::Math::Vec4 INVALID_COLOR{1, 0.2f, 0.2f, 1};
        constexpr Comet::Math::Vec4 DISABLED_COLOR{0.6f, 0.6f, 0.6f, 1};

        bool supported_collider_transform(const Comet::Entity entity,
            const Comet::ColliderComponent& collider, const Comet::TransformComponent& transform) {
            using namespace Comet;
            const bool positive_scale = glm::all(glm::greaterThan(transform.scale, Math::Vec3(0)));
            const bool uniform_scale =
                transform.scale.x == transform.scale.y && transform.scale.x == transform.scale.z;
            return entity.get_component<RelationshipComponent>().parent == INVALID_ENTITY_ID
                   && positive_scale
                   && (collider.shape == ColliderShape::Box
                       || (collider.shape == ColliderShape::Sphere && uniform_scale));
        }

        bool supported_joint_body(const Comet::Entity entity) {
            using namespace Comet;
            return entity && entity.has_component<RigidBodyComponent>()
                   && entity.has_component<ColliderComponent>()
                   && entity.has_component<TransformComponent>()
                   && supported_collider_transform(entity,
                       entity.get_component<ColliderComponent>(),
                       entity.get_component<TransformComponent>());
        }

        bool dynamic_body(const Comet::Entity entity) {
            return entity && entity.has_component<Comet::RigidBodyComponent>()
                   && entity.get_component<Comet::RigidBodyComponent>().motion
                          == Comet::BodyMotion::Dynamic;
        }

        Comet::Math::Vec4 collider_color(const Comet::Entity entity,
            const Comet::ColliderComponent& collider, const Comet::TransformComponent& transform) {
            using namespace Comet;
            if(!supported_collider_transform(entity, collider, transform))
                return INVALID_COLOR;
            if(!entity.has_component<RigidBodyComponent>())
                return DISABLED_COLOR;
            if(collider.is_trigger)
                return {0.1f, 0.9f, 0.9f, 1};
            switch(entity.get_component<RigidBodyComponent>().motion) {
                case BodyMotion::Static:
                    return {0.25f, 0.55f, 1, 1};
                case BodyMotion::Dynamic:
                    return {0.2f, 1, 0.3f, 1};
                case BodyMotion::Kinematic:
                    return {0.8f, 0.4f, 1, 1};
            }
            return INVALID_COLOR;
        }

        void append_joint_anchor(Comet::LineDrawList& lines, const Comet::Math::Vec3 position,
            const Comet::Math::Vec4 color) {
            for(int axis = 0; axis < 3; ++axis) {
                Comet::Math::Vec3 offset{0};
                offset[axis] = 0.06f;
                static_cast<void>(lines.add_line(position - offset, position + offset, color));
            }
        }
        void append_hinge_limits(Comet::LineDrawList& lines,
            const Comet::HingeJointComponent& joint, const Comet::Math::Vec3 anchor,
            const Comet::Math::Mat4& pose, const Comet::Math::Vec4 reference_color,
            const Comet::Math::Vec4 limit_color) {
            using namespace Comet;
            const auto reference =
                Math::perpendicular_direction(joint.connected_axis, joint.connected_reference_axis);
            const auto axis = Math::normalized_direction(joint.connected_axis);
            if(!reference || !axis || !joint.has_valid_angle_range())
                return;
            const auto world_reference = Math::Vec3(pose * Math::Vec4(*reference, 0));
            const auto world_axis = Math::Vec3(pose * Math::Vec4(*axis, 0));
            static_cast<void>(
                lines.add_line(anchor, anchor + world_reference * 0.14f, reference_color));
            constexpr int SEGMENTS = 32;
            const auto position = [&](float degrees) {
                return anchor
                       + (Math::angle_axis(Math::radians(degrees), world_axis) * world_reference)
                             * 0.16f;
            };
            auto previous = position(joint.min_angle);
            static_cast<void>(lines.add_line(anchor, previous, limit_color));
            for(int segment = 1; segment <= SEGMENTS; ++segment) {
                const float angle = glm::mix(
                    joint.min_angle, joint.max_angle, static_cast<float>(segment) / SEGMENTS);
                const auto next = position(angle);
                static_cast<void>(lines.add_line(previous, next, limit_color));
                previous = next;
            }
            static_cast<void>(lines.add_line(anchor, previous, limit_color));
        }

        void append_slider_limits(Comet::LineDrawList& lines,
            const Comet::SliderJointComponent& joint, const Comet::Math::Vec3 anchor,
            const Comet::Math::Mat4& pose, const Comet::Math::Vec4 reference_color,
            const Comet::Math::Vec4 limit_color) {
            using namespace Comet;
            const auto reference =
                Math::perpendicular_direction(joint.connected_axis, joint.connected_reference_axis);
            if(!reference)
                return;
            const auto normal = Math::Vec3(pose * Math::Vec4(*reference, 0));
            static_cast<void>(lines.add_line(anchor, anchor + normal * 0.14f, reference_color));
            const auto axis = Math::normalized_direction(joint.connected_axis);
            if(!joint.limits_enabled || !joint.has_valid_distance_range() || !axis)
                return;
            const auto direction = Math::Vec3(pose * Math::Vec4(*axis, 0));
            const auto minimum = anchor + direction * joint.min_distance;
            const auto maximum = anchor + direction * joint.max_distance;
            static_cast<void>(lines.add_line(minimum, maximum, limit_color));
            for(const auto end : {minimum, maximum})
                static_cast<void>(
                    lines.add_line(end - normal * 0.04f, end + normal * 0.04f, limit_color));
        }

        template<typename Joint>
        void append_joint_type_anchors(Comet::Scene& scene, Comet::LineDrawList& lines) {
            using namespace Comet;
            scene.each<const TransformComponent, const Joint>([&](const Entity entity,
                                                                  const TransformComponent&,
                                                                  const Joint& joint) {
                const auto& first_pose = scene.get_render_transform(entity);
                const auto& first_matrix = first_pose.world_matrix;
                const auto first_anchor = Math::Vec3(first_matrix * Math::Vec4(joint.anchor, 1));
                if(!Math::is_finite(first_anchor))
                    return;
                const Entity connected = scene.find_entity(joint.connected_body);
                bool supported = supported_joint_body(entity)
                                 && (!joint.connected_body || supported_joint_body(connected))
                                 && joint.connected_body != entity.get_uuid()
                                 && (dynamic_body(entity) || dynamic_body(connected));
                if constexpr(!std::is_same_v<Joint, PointJointComponent>) {
                    supported &= Math::normalized_direction(joint.axis).has_value()
                                 && Math::normalized_direction(joint.connected_axis).has_value();
                    if(std::is_same_v<Joint, SliderJointComponent> || joint.limits_enabled)
                        supported &= Math::perpendicular_direction(joint.axis, joint.reference_axis)
                                         .has_value()
                                     && Math::perpendicular_direction(
                                         joint.connected_axis, joint.connected_reference_axis)
                                            .has_value();
                    if(joint.limits_enabled) {
                        if constexpr(std::is_same_v<Joint, HingeJointComponent>)
                            supported &= joint.has_valid_angle_range();
                        else
                            supported &= joint.has_valid_distance_range();
                    }
                }
                Math::Vec4 first_color{1, 0.65f, 0.1f, 1};
                Math::Vec4 second_color{0.1f, 0.9f, 0.9f, 1};
                Math::Vec4 gap_color{1, 1, 0.2f, 1};
                if(!supported)
                    first_color = second_color = gap_color = INVALID_COLOR;
                if(!joint.enabled)
                    first_color = second_color = gap_color = DISABLED_COLOR;
                append_joint_anchor(lines, first_anchor, first_color);
                static_cast<void>(
                    lines.add_line(Math::Vec3(first_matrix[3]), first_anchor, first_color));
                // 非空引用无法解析时只显示本端，不伪造固定世界点。
                if(joint.connected_body
                    && (!connected || !connected.has_component<TransformComponent>()))
                    return;
                auto second_anchor = joint.connected_anchor;
                if(connected) {
                    const auto& second_matrix = scene.get_render_transform(connected).world_matrix;
                    second_anchor = Math::Vec3(second_matrix * Math::Vec4(second_anchor, 1));
                    static_cast<void>(
                        lines.add_line(Math::Vec3(second_matrix[3]), second_anchor, second_color));
                }
                constexpr Math::Vec3 half_size{0.035f};
                static_cast<void>(lines.add_box(
                    {.minimum = second_anchor - half_size, .maximum = second_anchor + half_size},
                    second_color));
                if constexpr(!std::is_same_v<Joint, PointJointComponent>) {
                    const auto first_axis = Math::normalized_direction(joint.axis);
                    const auto second_axis = Math::normalized_direction(joint.connected_axis);
                    if(first_axis) {
                        const auto direction =
                            Math::Vec3(first_pose.pose_world_matrix * Math::Vec4(*first_axis, 0));
                        static_cast<void>(lines.add_line(first_anchor - direction * 0.18f,
                            first_anchor + direction * 0.18f, first_color));
                    }
                    if(second_axis) {
                        auto direction = *second_axis;
                        if(connected)
                            direction =
                                Math::Vec3(scene.get_render_transform(connected).pose_world_matrix
                                           * Math::Vec4(direction, 0));
                        static_cast<void>(lines.add_line(second_anchor - direction * 0.18f,
                            second_anchor + direction * 0.18f, second_color));
                    }
                    if(std::is_same_v<Joint, SliderJointComponent> || joint.limits_enabled) {
                        const auto reference =
                            Math::perpendicular_direction(joint.axis, joint.reference_axis);
                        if(reference) {
                            const auto direction = Math::Vec3(
                                first_pose.pose_world_matrix * Math::Vec4(*reference, 0));
                            static_cast<void>(lines.add_line(
                                first_anchor, first_anchor + direction * 0.14f, first_color));
                        }
                        Math::Mat4 second_pose(1);
                        if(connected)
                            second_pose = scene.get_render_transform(connected).pose_world_matrix;
                        if constexpr(std::is_same_v<Joint, HingeJointComponent>) {
                            append_hinge_limits(
                                lines, joint, second_anchor, second_pose, second_color, gap_color);
                        } else
                            append_slider_limits(
                                lines, joint, second_anchor, second_pose, second_color, gap_color);
                    }
                }
                if(first_anchor != second_anchor)
                    static_cast<void>(lines.add_line(first_anchor, second_anchor, gap_color));
            });
        }
    }

    void append_collider_outlines(Comet::Scene& scene, Comet::LineDrawList& lines) {
        scene.each<const Comet::TransformComponent, const Comet::ColliderComponent>(
            [&](const Comet::Entity entity, const Comet::TransformComponent& transform,
                const Comet::ColliderComponent& collider) {
                const auto color = collider_color(entity, collider, transform);
                const auto& matrix = scene.get_render_transform(entity).world_matrix;
                switch(collider.shape) {
                    case Comet::ColliderShape::Box:
                        if(Comet::Math::is_finite(collider.half_extents)
                            && glm::all(
                                glm::greaterThan(collider.half_extents, Comet::Math::Vec3(0))))
                            static_cast<void>(lines.add_box({.minimum = -collider.half_extents,
                                                                .maximum = collider.half_extents},
                                matrix, color));
                        break;
                    case Comet::ColliderShape::Sphere:
                        static_cast<void>(lines.add_sphere(collider.radius, matrix, color));
                        break;
                }
            });
    }

    void append_joint_anchors(Comet::Scene& scene, Comet::LineDrawList& lines) {
        append_joint_type_anchors<Comet::PointJointComponent>(scene, lines);
        append_joint_type_anchors<Comet::HingeJointComponent>(scene, lines);
        append_joint_type_anchors<Comet::SliderJointComponent>(scene, lines);
    }
}
