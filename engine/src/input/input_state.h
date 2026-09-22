#pragma once

#include "input/input.h"

#include <map>
#include <string>
#include <string_view>

namespace Comet {
    // 同一授权与更新阶段的只读快照；不暴露绑定配置或分别安装物理／动作状态的接口。
    class COMET_API InputState final {
    public:
        struct Action {
            enum class Type { Button, Axis, Delta };
            Type type = Type::Button;
            float value = 0;
            bool down = false;
            bool pressed = false;
            bool released = false;
        };

        [[nodiscard]] bool focused() const { return m_physical.focused; }
        [[nodiscard]] const Input::Frame& physical() const { return m_physical; }
        [[nodiscard]] const Action* action(std::string_view name) const;

    private:
        friend class InputActions;
        friend class RuntimeInput;
        void clear_transients();

        Input::Frame m_physical;
        std::map<std::string, Action, std::less<>> m_actions;
    };
}
