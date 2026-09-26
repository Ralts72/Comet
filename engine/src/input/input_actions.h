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
            bool operator==(const Binding&) const = default;
        };
        struct Action {
            std::string name;
            Type type = Type::Button;
            std::vector<Binding> bindings;
            bool operator==(const Action&) const = default;
        };
        struct ControlName {
            std::string_view source;
            std::string control;
        };
        [[nodiscard]] static Result<InputActions> create(std::vector<Action> actions);
        [[nodiscard]] static Result<Binding> parse_binding(
            std::string_view source, std::string_view control, float scale = 1, float deadzone = 0);
        [[nodiscard]] static Result<ControlName> format_binding(const Binding& binding);
        [[nodiscard]] const std::vector<Action>& actions() const { return m_actions; }
        bool operator==(const InputActions&) const = default;
        // 同一配置下的电平历史与新快照；不同消费者分别持有，更换配置时清空。
        void evaluate(const Input::Frame& input, InputState& previous) const;

    private:
        std::vector<Action> m_actions;
    };
}
