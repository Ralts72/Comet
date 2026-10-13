#pragma once

#include "physics/physics_service.h"
#include "scene/entity.h"
#include "scene/systems/system.h"

#include <vector>

namespace Comet {
    class COMET_API PhysicsSystem final: public System {
    public:
        explicit PhysicsSystem(PhysicsService& physics) : m_physics(physics) {}

        Result<void, Error> on_start(
            Scene& scene, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> fixed_update(Scene& scene, const Context& context) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;
        [[nodiscard]] PhysicsService::Statistics get_statistics() const {
            return m_physics.get_statistics();
        }

    private:
        struct Entry {
            Entity entity;
            EntityId id;
            EntityUuid uuid;
            bool character = false;
        };
        Result<void, Error> synchronize(Scene& scene);
        Result<PhysicsService::ObjectChange, Error> synchronize_object(
            Scene& scene, entt::entity handle);
        Result<void, Error> synchronize_joints(Scene& scene);
        PhysicsService& m_physics;
        std::vector<Entry> m_entries;
    };
}
