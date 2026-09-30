#pragma once

#include "input/input_state.h"
#include "common/result.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Comet {
    class COMET_API InputActions final {
    public:
        static constexpr std::size_t MAX_ACTIONS = 128;
        static constexpr std::size_t MAX_BINDINGS = 16;
        static constexpr std::size_t MAX_CONTEXTS = 32;
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
            std::string context{};
            bool operator==(const Action&) const = default;
        };
        struct Context {
            std::string name;
            bool enabled = true;
            bool operator==(const Context&) const = default;
        };
        struct ControlName {
            std::string_view source;
            std::string control;
        };
        [[nodiscard]] static bool valid_name(std::string_view name);
        [[nodiscard]] static Result<InputActions> create(
            std::vector<Action> actions, std::vector<Context> contexts = {});
        [[nodiscard]] static Result<Binding> parse_binding(
            std::string_view source, std::string_view control, float scale = 1, float deadzone = 0);
        [[nodiscard]] static Result<ControlName> format_binding(const Binding& binding);
        [[nodiscard]] const std::vector<Action>& actions() const { return m_actions; }
        [[nodiscard]] const std::vector<Context>& contexts() const { return m_contexts; }
        bool operator==(const InputActions&) const = default;
        // 同一配置下的电平历史与新快照；不同消费者分别持有，更换配置时清空。
        void evaluate(const Input::Frame& input, InputState& previous) const;

    private:
        friend class RuntimeInput;
        void evaluate(const Input::Frame& input, InputState& previous,
            std::span<const Context> contexts) const;

        std::vector<Action> m_actions;
        std::vector<Context> m_contexts;
    };
}
