#pragma once

#include "input/input_state.h"
#include "common/result.h"
#include "common/uuid.h"

#include <bitset>
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
            Uuid id{};
            bool operator==(const Binding&) const = default;
        };
        struct Action {
            std::string name;
            Type type = Type::Button;
            std::vector<Binding> bindings;
            std::string context{};
            Uuid id{};
            bool operator==(const Action&) const = default;
        };
        struct Context {
            std::string name;
            bool enabled = true;
            int priority = 0;
            bool consume = false;
            bool operator==(const Context&) const = default;
        };
        enum class BindingRelation { Unrelated, Shared, Consumes, ConsumedBy };
        struct ControlName {
            std::string_view source;
            std::string control;
        };
        [[nodiscard]] static bool valid_name(std::string_view name);
        [[nodiscard]] static Result<InputActions> create(
            std::vector<Action> actions, std::vector<Context> contexts = {});
        // 匿名配置仅用于运行时；持久配置要求动作和绑定都具备非零身份。
        [[nodiscard]] Result<void> validate_persistent_ids() const;
        [[nodiscard]] static Result<Binding> parse_binding(
            std::string_view source, std::string_view control, float scale = 1, float deadzone = 0);
        [[nodiscard]] static Result<ControlName> format_binding(const Binding& binding);
        // 两组同时启用时的两两关系；空组是 Common，忽略 enabled，不代表最终路由。
        [[nodiscard]] static BindingRelation compare_bindings(const Binding& binding,
            const Context* context, const Binding& other, const Context* other_context);
        [[nodiscard]] const std::vector<Action>& actions() const { return m_actions; }
        [[nodiscard]] const std::vector<Context>& contexts() const { return m_contexts; }
        bool operator==(const InputActions&) const = default;
        // 同一配置下的电平历史与新快照；不同消费者分别持有，更换配置时清空。
        void evaluate(const Input::Frame& input, InputState& previous) const;

    private:
        friend class RuntimeInput;
        using BindingMask = std::bitset<MAX_BINDINGS>;
        using Routing = std::vector<BindingMask>;
        struct Sample {
            double value = 0;
            Input::ButtonState button;
            bool available = false;
            // Gamepad 绑定读取首个已连接槽位；换槽时不能沿用旧设备的待消费边沿。
            std::size_t gamepad = Input::MAX_GAMEPADS;
        };
        using Samples = std::vector<std::vector<Sample>>;

        [[nodiscard]] Routing resolve_routes(std::span<const Context> contexts) const;
        void sample(const Input::Frame& input, const Routing& routes, Samples& samples) const;
        [[nodiscard]] static Sample sample_binding(
            const Binding& binding, const Input::Frame& input, std::size_t gamepad);
        void evaluate_samples(
            const Input::Frame& input, const Samples& samples, InputState& previous) const;
        void clear_transients(Samples& samples) const;

        std::vector<Action> m_actions;
        std::vector<Context> m_contexts;
    };
}
