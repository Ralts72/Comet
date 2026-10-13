#pragma once

#include "common/export.h"
#include "common/result.h"
#include "scene/entity_uuid.h"

#include <string_view>

namespace Comet {
    class Entity;
    class InputState;
    class PhysicsAccess;
    class Scene;
}

namespace Comet::Gameplay {
    // 动作名借用项目配置；控制器不持有场景、输入快照或物理运行态。
    struct CharacterController {
        enum class Motion { Idle, Walking, Running, Crouching, Jumping, Falling };
        struct State {
            Motion motion = Motion::Idle;
            bool grounded = false;
            bool crouched = false;
            float speed = 0;
            float vertical_speed = 0;
            float height_ratio = 1;
            [[nodiscard]] COMET_API std::string_view name() const;
        };
        std::string_view move_x = "character.move_x";
        std::string_view move_z = "character.move_z";
        std::string_view jump = "character.jump";
        // 未指定动作时停用对应功能，项目显式选择跑步／蹲伏绑定。
        std::string_view run;
        std::string_view crouch;
        // 空引用使用世界 XZ；朝向只写可视实体，不接管物理实体的变换。
        EntityUuid movement_reference{};
        EntityUuid facing{};

        // 每个固定步调用；返回是否提交了地面跳跃请求，供玩法触发声音等反馈。
        [[nodiscard]] COMET_API Result<bool> update(Scene& scene, Entity entity,
            const InputState& input, PhysicsAccess& physics, bool enabled = true) const;
        [[nodiscard]] static COMET_API Result<State> get_state(
            Entity entity, const PhysicsAccess& physics);
    };
}
