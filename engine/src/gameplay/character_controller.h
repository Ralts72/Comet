#pragma once

#include "common/export.h"
#include "common/result.h"

#include <string_view>

namespace Comet {
    class Entity;
    class InputState;
    class PhysicsAccess;
}

namespace Comet::Gameplay {
    // 动作名借用项目配置；控制器不持有场景、输入快照或物理运行态。
    struct CharacterController {
        std::string_view move_x = "character.move_x";
        std::string_view move_z = "character.move_z";
        std::string_view jump = "character.jump";

        // 每个固定步调用；返回是否提交了地面跳跃请求，供玩法触发声音等反馈。
        [[nodiscard]] COMET_API Result<bool> update(Entity entity, const InputState& input,
            PhysicsAccess& physics, bool enabled = true) const;
    };
}
