#pragma once

#include "physics/physics_service.h"
#include "scene/entity.h"
#include "scene/systems/system.h"

#include <map>

namespace Comet {
    class COMET_API PhysicsSystem final: public System {
    public:
        explicit PhysicsSystem(PhysicsService& physics) : m_physics(physics) {}

        Result<void, Error> on_start(
            Scene& scene, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> fixed_update(Scene& scene, const Context& context) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        struct Entry {
            Entity entity;
            EntityId id;
        };
        Result<void, Error> synchronize(Scene& scene, float delta_time);
        PhysicsService& m_physics;
        std::map<EntityUuid, Entry> m_entries;
    };
}
