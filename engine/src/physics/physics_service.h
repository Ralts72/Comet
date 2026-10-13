#pragma once

#include "physics/physics_access.h"
#include "common/error.h"
#include "common/result.h"
#include "scene/components.h"

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace Comet {
    class PhysicsSystem;

    class COMET_API PhysicsService final: public PhysicsAccess {
    public:
        struct Statistics {
            std::size_t bodies = 0;
            std::size_t active_bodies = 0;
            std::size_t pose_updates = 0;
            std::size_t joints = 0;
            std::size_t characters = 0;
        };

        PhysicsService();
        ~PhysicsService() override;
        // 主线程在固定步之外读取；pose_updates 对应最近完成的固定步。
        [[nodiscard]] Statistics get_statistics() const;

    private:
        friend class PhysicsSystem;
        static constexpr std::size_t MAX_COMMANDS = 128;
        enum class ObjectChange { None, Added, Rebuilt };
        struct BodyDefinition {
            // 仅在同步调用内借用组件，后端缓存独立值。
            EntityUuid uuid;
            EntityId entity;
            const TransformComponent& transform;
            const ColliderComponent& collider;
            const RigidBodyComponent& rigid;
        };
        struct CharacterDefinition {
            EntityUuid uuid;
            EntityId entity;
            const TransformComponent& transform;
            const CharacterControllerComponent& settings;
        };
        struct Pose {
            EntityId entity;
            TransformComponent transform;
        };
        using JointSettings =
            std::variant<PointJointComponent, HingeJointComponent, SliderJointComponent>;
        using JointKey = std::pair<EntityId, std::size_t>;
        struct JointDefinition {
            EntityUuid uuid;
            EntityId entity;
            EntityId connected_entity;
            JointSettings joint;
            [[nodiscard]] JointKey key() const { return {entity, joint.index()}; }
        };
        struct JointBreak {
            EntityId entity;
            EntityUuid uuid;
            JointSettings joint;
            float force;
            float torque;
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
        struct CharacterRequest {
            EntityUuid uuid;
            EntityId entity;
            CharacterMove move;
        };

        bool enqueue_command(EntityUuid uuid, EntityId entity, CommandKind kind, Math::Vec3 value,
            std::optional<Math::Vec3> point) override;
        std::optional<Velocity> read_velocity(EntityUuid uuid, EntityId entity) const override;
        bool enqueue_character_move(EntityUuid uuid, EntityId entity, CharacterMove move) override;
        std::optional<CharacterState> read_character_state(
            EntityUuid uuid, EntityId entity) const override;
        std::optional<float> cast_sphere(EntityUuid ignored, EntityId entity, Math::Vec3 origin,
            Math::Vec3 displacement, float radius) const override;
        void reset() noexcept override;
        Result<void, Error> prepare_world();
        // 区分首次登记与重建，重建后需重新绑定关节。
        Result<ObjectChange, Error> synchronize_body(const BodyDefinition& definition);
        Result<ObjectChange, Error> synchronize_character(const CharacterDefinition& definition);
        void remove_characters(std::span<const std::pair<EntityUuid, EntityId>> definitions);
        // 按实体与关节类型排序；仅配置／结构变化或刚体重建时调用。
        Result<void, Error> synchronize_joints(std::span<const JointDefinition> definitions);
        void remove_bodies(std::span<const std::pair<EntityUuid, EntityId>> definitions);
        Result<void, Error> step(float delta_time);
        [[nodiscard]] std::span<const Pose> poses() const;
        [[nodiscard]] std::span<const ContactChange> contacts() const;
        [[nodiscard]] std::span<const JointBreak> broken_joints() const;

        struct Impl;
        std::unique_ptr<Impl> m_impl;
        std::vector<Command> m_commands;
        std::vector<CharacterRequest> m_character_moves;
    };
}
