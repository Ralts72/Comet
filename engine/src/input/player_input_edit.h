#pragma once

#include "input/input_overrides.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Comet {
    // 玩家改键事务；呈现层决定输入归属，宿主处理持久化和 Runtime 应用。
    class COMET_API PlayerInputEdit final {
    public:
        enum class CaptureKind { Keyboard, GamepadButton };
        struct Capture {
            Uuid action;
            Uuid binding;
            std::uint64_t interruption;
            std::uint64_t serial;
            std::optional<std::size_t> gamepad;
            std::uint64_t gamepad_connection_revision = 0;
        };

        void reset(const InputActions& defaults, const InputOverrides& current,
            std::span<const Input::Key> reserved_keys = {});
        void clear();
        [[nodiscard]] const InputActions& defaults() const { return m_defaults; }
        [[nodiscard]] const InputOverrides& draft() const { return m_draft; }
        [[nodiscard]] std::span<const Input::Key> reserved_keys() const { return m_reserved_keys; }
        [[nodiscard]] const std::string& error() const { return m_error; }
        [[nodiscard]] bool waiting() const { return m_waiting; }
        [[nodiscard]] const std::optional<Capture>& capture() const { return m_capture; }
        [[nodiscard]] const Result<InputOverrides::Resolution>& resolution() const {
            return m_resolution;
        }
        [[nodiscard]] const InputOverrides::Action* action_patch(Uuid action) const;
        [[nodiscard]] InputOverrides::Binding binding_patch(Uuid action, Uuid binding) const;
        [[nodiscard]] bool is_reserved(const InputActions::Control& control) const;

        void restore_action(Uuid action);
        void restore_binding(Uuid action, Uuid binding);
        void restore_all();
        void disable_action(Uuid action, bool disabled);
        void disable_binding(Uuid action, Uuid binding, bool disabled);
        void change_control(Uuid action, Uuid binding, InputActions::Control control);
        void change_scale(Uuid action, Uuid binding, float scale);
        void change_deadzone(Uuid action, Uuid binding, float deadzone);
        void report_error(std::string error);

        void start_capture(Uuid action, Uuid binding, const Input::Frame& input, CaptureKind kind);
        void capture_input(const Input::Frame& input, bool interaction_allowed);
        void cancel_capture();
        void apply();
        [[nodiscard]] std::optional<InputOverrides> take_request();
        // 只有待提交事务成功时返回 true，呈现层据此关闭界面。
        [[nodiscard]] bool complete(const Result<void>& result);

    private:
        using Action = InputActions::Action;
        using Binding = InputActions::Binding;
        [[nodiscard]] const Action* find_action(Uuid action);
        [[nodiscard]] const Binding* find_binding(const Action& action, Uuid binding);
        void commit(std::vector<InputOverrides::Action> actions);
        void commit_binding(
            const Action& action, const Binding& binding, InputOverrides::Binding patch);
        void store_binding(
            const Action& action, const Binding& binding, InputOverrides::Binding patch);

        InputActions m_defaults;
        InputOverrides m_draft;
        Result<InputOverrides::Resolution> m_resolution =
            Result<InputOverrides::Resolution>::success({});
        std::vector<Input::Key> m_reserved_keys;
        std::optional<InputOverrides> m_request;
        std::optional<Capture> m_capture;
        std::string m_error;
        bool m_waiting = false;
    };
}
