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
        const auto magnitude = std::hypot(
            static_cast<double>(move.direction.x), static_cast<double>(move.direction.y));
        if(magnitude > 1)
            move.direction = {static_cast<float>(move.direction.x / magnitude),
                static_cast<float>(move.direction.y / magnitude)};
        return enqueue_character_move(entity.get_uuid(), entity.get_id(), move);
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
        if(!m_scene || (ignored != Entity{} && !m_scene->is_valid(ignored))
            || !Math::is_finite(origin) || !Math::is_finite(displacement) || !std::isfinite(radius)
            || radius <= 0)
            return std::nullopt;
        return cast_sphere(ignored.get_uuid(), ignored.get_id(), origin, displacement, radius);
    }
}
