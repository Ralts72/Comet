#pragma once

#include "physics/physics_commands.h"
#include "common/error.h"
#include "common/result.h"
#include "scene/components.h"

#include <memory>
#include <span>
#include <vector>

namespace Comet {
    class PhysicsSystem;

    class COMET_API PhysicsService final: public PhysicsCommands {
    public:
        PhysicsService();
        ~PhysicsService() override;

    private:
        friend class PhysicsSystem;
        static constexpr std::size_t MAX_IMPULSES = 128;
        struct BodyDefinition {
            EntityUuid uuid;
            EntityId entity;
            TransformComponent transform;
            ColliderComponent collider;
            RigidBodyComponent rigid;
        };
        struct Pose {
            EntityId entity;
            TransformComponent transform;
        };
        struct ContactChange {
            enum class Kind { CollisionEnter, CollisionExit, TriggerEnter, TriggerExit } kind;
            EntityId first;
            EntityId second;
            EntityUuid first_uuid;
            EntityUuid second_uuid;
        };
        struct Impulse {
            EntityUuid uuid;
            EntityId entity;
            Math::Vec3 value;
        };

        bool enqueue_impulse(EntityUuid uuid, EntityId entity, Math::Vec3 impulse) override;
        void reset() noexcept override;
        Result<void, Error> prepare_world();
        Result<void, Error> synchronize_body(const BodyDefinition& definition, float delta_time);
        void remove_body(EntityUuid uuid, EntityId entity);
        Result<void, Error> step(float delta_time);
        [[nodiscard]] std::span<const Pose> poses() const;
        [[nodiscard]] std::span<const ContactChange> contacts() const;

        struct Impl;
        std::unique_ptr<Impl> m_impl;
        std::vector<Impulse> m_impulses;
    };
}
