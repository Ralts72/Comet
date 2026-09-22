#pragma once

#include "input/input_state.h"
#include "common/result.h"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Comet {
    class COMET_API InputActions final {
    public:
        using Type = InputState::Action::Type;
        enum class Motion { CursorX, CursorY, ScrollX, ScrollY };
        struct Binding {
            std::variant<Input::Key, Input::MouseButton, Input::GamepadButton, Input::GamepadAxis,
                Motion>
                control;
            float scale = 1;
            float deadzone = 0;
        };
        struct Action {
            std::string name;
            Type type = Type::Button;
            std::vector<Binding> bindings;
        };
        [[nodiscard]] static Result<InputActions> create(std::vector<Action> actions);
        [[nodiscard]] static Result<Binding> parse_binding(
            std::string_view source, std::string_view control, float scale = 1, float deadzone = 0);
        // 同一配置下的电平历史与新快照；不同消费者分别持有，更换配置时清空。
        void evaluate(const Input::Frame& input, InputState& previous) const;

    private:
        std::vector<Action> m_actions;
    };
}
