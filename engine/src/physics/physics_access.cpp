#include "physics/physics_access.h"
#include "scene/scene.h"

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
}
