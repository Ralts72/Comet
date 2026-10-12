#pragma once

#include "physics/physics_access.h"
#include "common/error.h"
#include "common/result.h"
#include "scene/components.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace Comet {
    class PhysicsSystem;

    class COMET_API PhysicsService final: public PhysicsAccess {
    public:
        struct Statistics {
            std::size_t bodies = 0;
            std::size_t active_bodies = 0;
            std::size_t pose_updates = 0;
        };

        PhysicsService();
        ~PhysicsService() override;
        // 主线程在固定步之外读取；pose_updates 对应最近完成的固定步。
        [[nodiscard]] Statistics get_statistics() const;

    private:
        friend class PhysicsSystem;
        static constexpr std::size_t MAX_COMMANDS = 128;
        struct BodyDefinition {
            // 仅在同步调用内借用组件，后端缓存独立值。
            EntityUuid uuid;
            EntityId entity;
            const TransformComponent& transform;
            const ColliderComponent& collider;
            const RigidBodyComponent& rigid;
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
        struct Command {
            EntityUuid uuid;
            EntityId entity;
            CommandKind kind;
            Math::Vec3 value;
            std::optional<Math::Vec3> point;
        };

        bool enqueue_command(EntityUuid uuid, EntityId entity, CommandKind kind, Math::Vec3 value,
            std::optional<Math::Vec3> point) override;
        std::optional<Velocity> read_velocity(EntityUuid uuid, EntityId entity) const override;
        void reset() noexcept override;
        Result<void, Error> prepare_world();
        // 首次登记 EntityId 返回 true；同实体的刚体重建不重复登记。
        Result<bool, Error> synchronize_body(const BodyDefinition& definition);
        void remove_body(EntityUuid uuid, EntityId entity);
        Result<void, Error> step(float delta_time);
        [[nodiscard]] std::span<const Pose> poses() const;
        [[nodiscard]] std::span<const ContactChange> contacts() const;

        struct Impl;
        std::unique_ptr<Impl> m_impl;
        std::vector<Command> m_commands;
    };
}
