#pragma once

#include "common/export.h"
#include "core/math_utils.h"
#include "scene/entity_id.h"
#include "scene/entity_uuid.h"
#include <optional>

namespace Comet {
    class Entity;
    class Scene;
    class SceneRuntime;
    class PhysicsService;

    // 玩法借用的物理接口；世界创建、同步与步进仍由 PhysicsService／PhysicsSystem 拥有。
    class COMET_API PhysicsAccess {
    public:
        struct Velocity {
            Math::Vec3 linear{0};
            Math::Vec3 angular{0};
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
        // 最近的模拟状态，m/s 与 rad/s；尚未登记、缺少组件或不属于本运行域时返回空值。
        [[nodiscard]] std::optional<Velocity> get_velocity(Entity entity) const;

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
        bool request(Entity entity, CommandKind kind, Math::Vec3 value);
        virtual bool enqueue_command(
            EntityUuid uuid, EntityId entity, CommandKind kind, Math::Vec3 value) = 0;
        virtual std::optional<Velocity> read_velocity(EntityUuid uuid, EntityId entity) const = 0;
        virtual void reset() noexcept = 0;

        const Scene* m_scene = nullptr;
    };
}
