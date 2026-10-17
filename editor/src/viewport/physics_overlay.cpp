#include "viewport/physics_overlay.h"
#include "render/debug/line_draw_list.h"
#include "scene/scene.h"
#include "diagnostics/logger.h"

namespace CometEditor {
    namespace {
        constexpr Comet::Math::Vec4 INVALID_COLOR{1, 0.2f, 0.2f, 1};
        constexpr Comet::Math::Vec4 DISABLED_COLOR{0.6f, 0.6f, 0.6f, 1};
        constexpr Comet::Math::Vec4 TARGET_COLOR{0.4f, 1, 0.4f, 1};

        bool supported_collider_transform(Comet::Scene& scene, const Comet::Entity entity,
            const Comet::ColliderComponent& collider, const Comet::TransformComponent& transform) {
            using namespace Comet;
            auto scale = transform.scale;
            bool uniform_scale = scale.x == scale.y && scale.x == scale.z;
            if(entity.get_component<RelationshipComponent>().parent != INVALID_ENTITY_ID) {
                if(!entity.has_component<RigidBodyComponent>()
                    || entity.get_component<RigidBodyComponent>().motion != BodyMotion::Static)
                    return false;
                const auto& matrix = scene.get_render_transform(entity).world_matrix;
                const auto world_scale = Math::positive_trs_scale(matrix);
                if(!world_scale)
                    return false;
                scale = *world_scale;
                uniform_scale = Math::uniform_scale(matrix).has_value();
            }
            const bool positive_scale = Math::all_greater(scale, Math::Vec3(0));
            return positive_scale
                   && (collider.shape == ColliderShape::Box
                       || (collider.uses_mesh() && collider.mesh
                           && (collider.shape != ColliderShape::TriangleMesh
                               || !entity.has_component<RigidBodyComponent>()
                               || entity.get_component<RigidBodyComponent>().motion
                                      == BodyMotion::Static))
                       || ((collider.shape == ColliderShape::Sphere
                               || collider.shape == ColliderShape::Capsule)
                           && uniform_scale));
        }

        bool supported_joint_body(Comet::Scene& scene, const Comet::Entity entity) {
            using namespace Comet;
            return entity && entity.has_component<RigidBodyComponent>()
                   && entity.has_component<ColliderComponent>()
                   && entity.has_component<TransformComponent>()
                   && supported_collider_transform(scene, entity,
                       entity.get_component<ColliderComponent>(),
                       entity.get_component<TransformComponent>());
        }

        bool dynamic_body(const Comet::Entity entity) {
            return entity && entity.has_component<Comet::RigidBodyComponent>()
                   && entity.get_component<Comet::RigidBodyComponent>().motion
                          == Comet::BodyMotion::Dynamic;
        }

        Comet::Math::Vec4 collider_color(Comet::Scene& scene, const Comet::Entity entity,
            const Comet::ColliderComponent& collider, const Comet::TransformComponent& transform) {
            using namespace Comet;
            if(!supported_collider_transform(scene, entity, collider, transform))
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
        void append_hinge_controls(Comet::LineDrawList& lines,
            const Comet::HingeJointComponent& joint, const Comet::Math::Vec3 anchor,
            const Comet::Math::Mat4& pose, const Comet::Math::Vec4 reference_color,
            const Comet::Math::Vec4 limit_color, const Comet::Math::Vec4 target_color) {
            using namespace Comet;
            const auto reference =
                Math::perpendicular_direction(joint.connected_axis, joint.connected_reference_axis);
            const auto axis = Math::normalized_direction(joint.connected_axis);
            if(!reference || !axis)
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
            if(joint.limits_enabled && joint.has_valid_angle_range()) {
                auto previous = position(joint.min_angle);
                static_cast<void>(lines.add_line(anchor, previous, limit_color));
                for(int segment = 1; segment <= SEGMENTS; ++segment) {
                    const float angle = Math::mix(
                        joint.min_angle, joint.max_angle, static_cast<float>(segment) / SEGMENTS);
                    const auto next = position(angle);
                    static_cast<void>(lines.add_line(previous, next, limit_color));
                    previous = next;
                }
                static_cast<void>(lines.add_line(anchor, previous, limit_color));
            }
            if(joint.motor_enabled && joint.motor_mode == JointMotorMode::Position
                && std::isfinite(joint.motor_target)) {
                float target = joint.motor_target;
                if(joint.limits_enabled && joint.has_valid_angle_range())
                    target = Math::clamp(target, joint.min_angle, joint.max_angle);
                static_cast<void>(lines.add_line(anchor, position(target), target_color));
            }
        }

        void append_slider_controls(Comet::LineDrawList& lines,
            const Comet::SliderJointComponent& joint, const Comet::Math::Vec3 anchor,
            const Comet::Math::Mat4& pose, const Comet::Math::Vec4 reference_color,
            const Comet::Math::Vec4 limit_color, const Comet::Math::Vec4 target_color) {
            using namespace Comet;
            const auto reference =
                Math::perpendicular_direction(joint.connected_axis, joint.connected_reference_axis);
            if(!reference)
                return;
            const auto normal = Math::Vec3(pose * Math::Vec4(*reference, 0));
            static_cast<void>(lines.add_line(anchor, anchor + normal * 0.14f, reference_color));
            const auto axis = Math::normalized_direction(joint.connected_axis);
            if(!axis)
                return;
            const auto direction = Math::Vec3(pose * Math::Vec4(*axis, 0));
            if(joint.limits_enabled && joint.has_valid_distance_range()) {
                const auto minimum = anchor + direction * joint.min_distance;
                const auto maximum = anchor + direction * joint.max_distance;
                static_cast<void>(lines.add_line(minimum, maximum, limit_color));
                for(const auto end : {minimum, maximum})
                    static_cast<void>(
                        lines.add_line(end - normal * 0.04f, end + normal * 0.04f, limit_color));
            }
            if(joint.motor_enabled && joint.motor_mode == JointMotorMode::Position
                && std::isfinite(joint.motor_target)) {
                float target = joint.motor_target;
                if(joint.limits_enabled && joint.has_valid_distance_range())
                    target = Math::clamp(target, joint.min_distance, joint.max_distance);
                const auto position = anchor + direction * target;
                static_cast<void>(lines.add_line(
                    position - normal * 0.04f, position + normal * 0.04f, target_color));
            }
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
                bool supported =
                    supported_joint_body(scene, entity)
                    && (!joint.connected_body || supported_joint_body(scene, connected))
                    && joint.connected_body != entity.get_uuid()
                    && (dynamic_body(entity) || dynamic_body(connected));
                if constexpr(!std::is_same_v<Joint, PointJointComponent>) {
                    supported &= Math::normalized_direction(joint.axis).has_value()
                                 && Math::normalized_direction(joint.connected_axis).has_value();
                    if(std::is_same_v<Joint, SliderJointComponent> || joint.limits_enabled
                        || (joint.motor_enabled && joint.motor_mode == JointMotorMode::Position))
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
                auto target_color = TARGET_COLOR;
                if(!supported)
                    first_color = second_color = gap_color = target_color = INVALID_COLOR;
                if(!joint.enabled)
                    first_color = second_color = gap_color = target_color = DISABLED_COLOR;
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
                    if(std::is_same_v<Joint, SliderJointComponent> || joint.limits_enabled
                        || (joint.motor_enabled && joint.motor_mode == JointMotorMode::Position)) {
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
                            append_hinge_controls(lines, joint, second_anchor, second_pose,
                                second_color, gap_color, target_color);
                        } else
                            append_slider_controls(lines, joint, second_anchor, second_pose,
                                second_color, gap_color, target_color);
                    }
                }
                if(first_anchor != second_anchor)
                    static_cast<void>(lines.add_line(first_anchor, second_anchor, gap_color));
            });
        }
    }

    void ColliderOverlay::append(Comet::Scene& scene, Comet::LineDrawList& lines) {
        for(auto& [key, outline] : m_cache)
            outline.used = false;
        scene.each<const Comet::TransformComponent, const Comet::CharacterControllerComponent>(
            [&](const Comet::Entity entity, const Comet::TransformComponent& transform,
                const Comet::CharacterControllerComponent& character) {
                using namespace Comet;
                auto color = Math::Vec4(1, 0.65f, 0.1f, 1);
                if(!character.enabled)
                    color = DISABLED_COLOR;
                else if(transform.scale != Math::Vec3(transform.scale.x) || transform.scale.x <= 0
                        || transform.rotation.x != 0 || transform.rotation.z != 0
                        || entity.has_component<RigidBodyComponent>()
                        || entity.get_component<RelationshipComponent>().parent
                               != INVALID_ENTITY_ID)
                    color = INVALID_COLOR;
                const auto matrix =
                    scene.get_render_transform(entity).world_matrix
                    * Math::translate(Math::Mat4(1), Math::Vec3(0, character.height * 0.5f, 0));
                static_cast<void>(lines.add_capsule(
                    character.radius, character.height * 0.5f - character.radius, matrix, color));
            });
        scene.each<const Comet::TransformComponent, const Comet::ColliderComponent>(
            [&](const Comet::Entity entity, const Comet::TransformComponent& transform,
                const Comet::ColliderComponent& collider) {
                const auto color = collider_color(scene, entity, collider, transform);
                const auto& matrix = scene.get_render_transform(entity).world_matrix;
                switch(collider.shape) {
                    case Comet::ColliderShape::Box:
                        if(Comet::Math::is_finite(collider.half_extents)
                            && Comet::Math::all_greater(
                                collider.half_extents, Comet::Math::Vec3(0)))
                            static_cast<void>(lines.add_box({.minimum = -collider.half_extents,
                                                                .maximum = collider.half_extents},
                                matrix, color));
                        break;
                    case Comet::ColliderShape::Capsule:
                        static_cast<void>(lines.add_capsule(collider.radius,
                            collider.height * 0.5f - collider.radius, matrix, color));
                        break;
                    case Comet::ColliderShape::Sphere:
                        static_cast<void>(lines.add_sphere(collider.radius, matrix, color));
                        break;
                    case Comet::ColliderShape::ConvexHull:
                    case Comet::ColliderShape::TriangleMesh:
                        append_mesh(collider, transform.scale, matrix, color, lines);
                        break;
                }
            });
        std::erase_if(m_cache, [](const auto& pair) { return !pair.second.used; });
    }

    void ColliderOverlay::append_mesh(const Comet::ColliderComponent& collider,
        Comet::Math::Vec3 scale, const Comet::Math::Mat4& matrix, Comet::Math::Vec4 color,
        Comet::LineDrawList& lines) {
        using namespace Comet;
        if(!Math::is_finite(scale) || !Math::all_greater(scale, Math::Vec3(0))) {
            append_joint_anchor(lines, Math::Vec3(matrix[3]), INVALID_COLOR);
            return;
        }
        const Key key{collider.mesh, collider.shape, scale.x, scale.y, scale.z};
        const auto revision = m_meshes.revision ? m_meshes.revision(collider.mesh) : 0;
        auto [it, inserted] = m_cache.try_emplace(key);
        auto& outline = it->second;
        if(inserted || outline.revision != revision) {
            outline.revision = revision;
            outline.edges.clear();
            auto build = [&]() -> Result<std::vector<PhysicsService::ColliderEdge>, Error> {
                if(!m_meshes.read)
                    return Result<std::vector<PhysicsService::ColliderEdge>, Error>::failure(
                        {"Collision preview requires a CPU mesh source"});
                auto mesh = m_meshes.read(collider.mesh);
                if(!mesh)
                    return Result<std::vector<PhysicsService::ColliderEdge>, Error>::failure(
                        mesh.error());
                return PhysicsService::mesh_collider_edges(collider.shape, mesh.value(), scale);
            }();
            if(build)
                outline.edges = std::move(build).value();
            else
                LOG_WARN("Cannot preview collision mesh {}: {}", collider.mesh.value(),
                    build.error().message);
        }
        outline.used = true;
        if(outline.edges.empty()) {
            append_joint_anchor(lines, Math::Vec3(matrix[3]), INVALID_COLOR);
            return;
        }
        // 构建时已应用本地缩放，呈现矩阵仅负责剩余的姿态和父级变换。
        const auto pose = matrix * Math::scale(Math::Mat4(1), Math::Vec3(1) / scale);
        for(const auto& [first, second] : outline.edges)
            if(!lines.add_line(Math::Vec3(pose * Math::Vec4(first, 1)),
                   Math::Vec3(pose * Math::Vec4(second, 1)), color))
                return;
    }

    void append_joint_anchors(Comet::Scene& scene, Comet::LineDrawList& lines) {
        append_joint_type_anchors<Comet::PointJointComponent>(scene, lines);
        append_joint_type_anchors<Comet::HingeJointComponent>(scene, lines);
        append_joint_type_anchors<Comet::SliderJointComponent>(scene, lines);
    }
}
