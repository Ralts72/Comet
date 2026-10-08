#include "physics/physics_commands.h"
#include "scene/scene.h"

namespace Comet {
    bool PhysicsCommands::begin(const Scene& scene) {
        if(m_scene)
            return false;
        m_scene = &scene;
        return true;
    }

    void PhysicsCommands::end() noexcept {
        reset();
        m_scene = nullptr;
    }

    bool PhysicsCommands::request_impulse(const Entity entity, const Math::Vec3 impulse) {
        if(!m_scene || !m_scene->is_valid(entity) || !Math::is_finite(impulse)
            || !entity.has_component<TransformComponent>()
            || !entity.has_component<ColliderComponent>()
            || !entity.has_component<RigidBodyComponent>()
            || entity.get_component<RigidBodyComponent>().motion != BodyMotion::Dynamic)
            return false;
        return enqueue_impulse(entity.get_uuid(), entity.get_id(), impulse);
    }
}
