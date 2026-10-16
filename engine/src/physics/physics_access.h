#pragma once

#include "common/export.h"
#include "common/result.h"
#include "core/math_utils.h"
#include "scene/entity_id.h"
#include "scene/entity_uuid.h"
#include <optional>
#include <vector>

namespace Comet {
    class Entity;
    class Scene;
    class SceneRuntime;
    class PhysicsService;
    struct ColliderComponent;
    struct TransformComponent;

    // 玩法借用的物理接口；世界创建、同步与步进仍由 PhysicsService／PhysicsSystem 拥有。
    class COMET_API PhysicsAccess {
    public:
        struct Velocity {
            Math::Vec3 linear{0};
            Math::Vec3 angular{0};
        };
        struct CharacterState {
            Math::Vec3 velocity{0};
            Math::Vec3 ground_normal{0};
            bool grounded = false;
            // 实际位移速度扣除承载平台速度；用于区分阻挡、运动与被动跟随。
            Math::Vec3 relative_velocity{0};
            bool running = false;
            bool crouched = false;
            float height_ratio = 1;
            Math::Vec3 desired_velocity{0};
        };
        struct CharacterMove {
            Math::Vec2 direction{0};
            bool jump = false;
            bool run = false;
            bool crouch = false;
        };
        struct CharacterVelocity {
            Math::Vec3 velocity{0};
            bool horizontal = true;
            bool vertical = false;
        };
        struct QueryFilter {
            EntityUuid ignored;
            bool include_triggers = false;
            uint32_t layers = UINT32_MAX;
        };
        struct QueryHit {
            EntityUuid entity;
            Math::Vec3 position{0};
            Math::Vec3 normal{0};
            float fraction = 0;
        };
        virtual ~PhysicsAccess() = default;
        PhysicsAccess(const PhysicsAccess&) = delete;
        PhysicsAccess& operator=(const PhysicsAccess&) = delete;

        [[nodiscard]] bool is_bound_to(const Scene& scene) const noexcept {
            return m_scene == &scene;
        }
        [[nodiscard]] bool request_impulse(Entity entity, Math::Vec3 impulse);
        // 世界空间，质心处施力；每次请求只作用于下一固定步，持续施力需逐固定步提交。
        [[nodiscard]] bool request_force(Entity entity, Math::Vec3 force);
        [[nodiscard]] bool request_torque(Entity entity, Math::Vec3 torque);
        // 力／冲量与作用点均为世界空间；保存提交时的点，在下一固定步按实际质心计算力臂。
        [[nodiscard]] bool request_impulse_at(Entity entity, Math::Vec3 impulse, Math::Vec3 point);
        [[nodiscard]] bool request_force_at(Entity entity, Math::Vec3 force, Math::Vec3 point);
        // 最近的模拟状态，m/s 与 rad/s；尚未登记、缺少组件或不属于本运行域时返回空值。
        [[nodiscard]] std::optional<Velocity> get_velocity(Entity entity) const;
        // 世界 XZ 平面的移动意图；长度超过 1 时归一化，最近一次请求只用于下一固定步。
        [[nodiscard]] bool request_character_move(Entity entity, CharacterMove move);
        // 世界空间 m/s，只覆盖指定轴并保留本步跳跃／姿态请求；下一固定步后清除。
        [[nodiscard]] bool request_character_velocity(Entity entity, CharacterVelocity velocity);
        [[nodiscard]] std::optional<CharacterState> get_character_state(Entity entity) const;
        // 同步球体扫掠，忽略指定实体和触发器；无命中返回 1，无运行域或无效参数返回空值。
        [[nodiscard]] std::optional<float> sweep_sphere(
            Entity ignored, Math::Vec3 origin, Math::Vec3 displacement, float radius) const;
        // 最近同步的物理世界；displacement 是完整线段，结果为世界坐标。
        // 射线从实体内部出发时 fraction 为 0，normal 取线段反方向。
        [[nodiscard]] Result<std::optional<QueryHit>> raycast(
            Math::Vec3 origin, Math::Vec3 displacement, const QueryFilter& filter) const;
        // 按固定姿态平移盒／球／胶囊；仅使用 Collider 的几何字段。
        // 命中 position 位于目标表面，fraction 为形状中心走过的位移比例。
        [[nodiscard]] Result<std::optional<QueryHit>> shape_cast(const ColliderComponent& shape,
            const TransformComponent& transform, Math::Vec3 displacement,
            const QueryFilter& filter) const;
        // 返回按 UUID 排序且去重的命中实体；空结果与无效查询通过 Result 区分。
        [[nodiscard]] Result<std::vector<EntityUuid>> overlap_sphere(
            Math::Vec3 center, float radius, const QueryFilter& filter) const;

    protected:
        PhysicsAccess() = default;
        enum class CommandKind { Impulse, Force, Torque };
        [[nodiscard]] bool has_binding() const noexcept { return m_scene != nullptr; }

    private:
        friend class SceneRuntime;
        friend class PhysicsService;
        bool begin(const Scene& scene);
        void end() noexcept;
        bool has_body(Entity entity) const;
        bool request(Entity entity, CommandKind kind, Math::Vec3 value,
            std::optional<Math::Vec3> point = std::nullopt);
        virtual bool enqueue_command(EntityUuid uuid, EntityId entity, CommandKind kind,
            Math::Vec3 value, std::optional<Math::Vec3> point) = 0;
        virtual std::optional<Velocity> read_velocity(EntityUuid uuid, EntityId entity) const = 0;
        virtual bool enqueue_character_move(
            EntityUuid uuid, EntityId entity, CharacterMove move) = 0;
        virtual bool enqueue_character_velocity(
            EntityUuid uuid, EntityId entity, CharacterVelocity velocity) = 0;
        virtual std::optional<CharacterState> read_character_state(
            EntityUuid uuid, EntityId entity) const = 0;
        virtual Result<std::optional<QueryHit>> query_shape(const ColliderComponent& shape,
            const TransformComponent& transform, Math::Vec3 displacement,
            const QueryFilter& filter) const = 0;
        virtual Result<std::optional<QueryHit>> query_ray(
            Math::Vec3 origin, Math::Vec3 displacement, const QueryFilter& filter) const = 0;
        virtual Result<std::vector<EntityUuid>> query_overlap(
            Math::Vec3 center, float radius, const QueryFilter& filter) const = 0;
        virtual void reset() noexcept = 0;

        const Scene* m_scene = nullptr;
    };
}
