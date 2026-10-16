#include "physics/physics_access.h"
#include "scene/scene.h"
#include <cmath>

namespace Comet {
    bool PhysicsAccess::begin(const Scene& scene) {
        if(m_scene)
            return false;
        m_scene = &scene;
        return true;
    }

    void PhysicsAccess::end() noexcept {
        reset();
        m_scene = nullptr;
    }

    bool PhysicsAccess::has_body(const Entity entity) const {
        return m_scene && m_scene->is_valid(entity) && entity.has_component<TransformComponent>()
               && entity.has_component<ColliderComponent>()
               && entity.has_component<RigidBodyComponent>();
    }

    bool PhysicsAccess::request(const Entity entity, const CommandKind kind, const Math::Vec3 value,
        const std::optional<Math::Vec3> point) {
        if(!has_body(entity) || !Math::is_finite(value) || (point && !Math::is_finite(*point))
            || entity.get_component<RigidBodyComponent>().motion != BodyMotion::Dynamic)
            return false;
        return enqueue_command(entity.get_uuid(), entity.get_id(), kind, value, point);
    }

    bool PhysicsAccess::request_impulse(const Entity entity, const Math::Vec3 impulse) {
        return request(entity, CommandKind::Impulse, impulse);
    }

    bool PhysicsAccess::request_force(const Entity entity, const Math::Vec3 force) {
        return request(entity, CommandKind::Force, force);
    }

    bool PhysicsAccess::request_torque(const Entity entity, const Math::Vec3 torque) {
        return request(entity, CommandKind::Torque, torque);
    }

    bool PhysicsAccess::request_impulse_at(
        const Entity entity, const Math::Vec3 impulse, const Math::Vec3 point) {
        return request(entity, CommandKind::Impulse, impulse, point);
    }

    bool PhysicsAccess::request_force_at(
        const Entity entity, const Math::Vec3 force, const Math::Vec3 point) {
        return request(entity, CommandKind::Force, force, point);
    }

    std::optional<PhysicsAccess::Velocity> PhysicsAccess::get_velocity(const Entity entity) const {
        if(!has_body(entity))
            return std::nullopt;
        return read_velocity(entity.get_uuid(), entity.get_id());
    }

    bool PhysicsAccess::request_character_move(const Entity entity, CharacterMove move) {
        if(!m_scene || !m_scene->is_valid(entity)
            || !entity.has_component<CharacterControllerComponent>()
            || !entity.get_component<CharacterControllerComponent>().enabled
            || !Math::is_finite(move.direction))
            return false;
        move.direction = Math::clamp_unit_length(move.direction);
        return enqueue_character_move(entity.get_uuid(), entity.get_id(), move);
    }

    bool PhysicsAccess::request_character_velocity(
        const Entity entity, CharacterVelocity velocity) {
        if(!m_scene || !m_scene->is_valid(entity)
            || !entity.has_component<CharacterControllerComponent>()
            || !entity.get_component<CharacterControllerComponent>().enabled
            || !Math::is_finite(velocity.velocity)
            || !std::isfinite(glm::dot(velocity.velocity, velocity.velocity)))
            return false;
        return enqueue_character_velocity(entity.get_uuid(), entity.get_id(), velocity);
    }

    std::optional<PhysicsAccess::CharacterState> PhysicsAccess::get_character_state(
        const Entity entity) const {
        if(!m_scene || !m_scene->is_valid(entity)
            || !entity.has_component<CharacterControllerComponent>()
            || !entity.get_component<CharacterControllerComponent>().enabled)
            return std::nullopt;
        return read_character_state(entity.get_uuid(), entity.get_id());
    }

    std::optional<float> PhysicsAccess::sweep_sphere(const Entity ignored, const Math::Vec3 origin,
        const Math::Vec3 displacement, const float radius) const {
        if(!m_scene || (ignored != Entity{} && !m_scene->is_valid(ignored)))
            return std::nullopt;
        const ColliderComponent sphere{.shape = ColliderShape::Sphere, .radius = radius};
        const auto hit = shape_cast(
            sphere, {.translation = origin}, displacement, {.ignored = ignored.get_uuid()});
        if(!hit)
            return std::nullopt;
        return hit.value() ? hit.value()->fraction : 1.0f;
    }

    Result<std::optional<PhysicsAccess::QueryHit>> PhysicsAccess::raycast(
        Math::Vec3 origin, Math::Vec3 displacement, const QueryFilter& filter) const {
        const float length_squared = glm::dot(displacement, displacement);
        if(!m_scene || !Math::is_finite(origin) || !Math::is_finite(displacement)
            || !Math::is_finite(origin + displacement) || !std::isfinite(length_squared)
            || length_squared <= 0)
            return Result<std::optional<QueryHit>>::failure(
                "Raycast requires an active physics world and a finite nonzero segment");
        return query_ray(origin, displacement, filter);
    }

    Result<std::optional<PhysicsAccess::QueryHit>> PhysicsAccess::shape_cast(
        const ColliderComponent& shape, const TransformComponent& transform,
        Math::Vec3 displacement, const QueryFilter& filter) const {
        if(!m_scene || !Math::is_finite(displacement)
            || !Math::is_finite(transform.translation + displacement)
            || !std::isfinite(glm::dot(displacement, displacement)))
            return Result<std::optional<QueryHit>>::failure(
                "Shape cast requires an active physics world and a finite displacement");
        return query_shape(shape, transform, displacement, filter);
    }

    Result<std::vector<EntityUuid>> PhysicsAccess::overlap_sphere(
        Math::Vec3 center, float radius, const QueryFilter& filter) const {
        if(!m_scene || !Math::is_finite(center) || !std::isfinite(radius) || radius <= 0
            || !std::isfinite(radius * radius) || !Math::is_finite(center + Math::Vec3(radius))
            || !Math::is_finite(center - Math::Vec3(radius)))
            return Result<std::vector<EntityUuid>>::failure(
                "Overlap requires an active physics world and a finite positive sphere");
        return query_overlap(center, radius, filter);
    }
}
