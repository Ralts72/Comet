#pragma once

#include "input/input_overrides.h"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace CometUi {
    class PlayerInputPanel final {
    public:
        using Text = std::map<std::string, std::string, std::less<>>;

        void open(const Comet::InputActions& defaults, const Comet::InputOverrides& current,
            std::span<const Comet::Input::Key> reserved_keys = {});
        [[nodiscard]] bool is_open() const { return m_open; }
        void close();
        // 关闭当帧也阻断游戏输入，宿主仍使用原有 Gate。
        [[nodiscard]] bool render(const Comet::Input::Frame& input, const Text& translations = {});
        [[nodiscard]] std::optional<Comet::InputOverrides> take_request();
        void complete(const Comet::Result<void>& result);

    private:
        using Action = Comet::InputActions::Action;
        using Binding = Comet::InputActions::Binding;
        struct Capture {
            Comet::Uuid action;
            Comet::Uuid binding;
            std::uint64_t interruption;
            std::uint64_t serial;
            std::optional<std::size_t> gamepad;
            std::uint64_t gamepad_connection_revision = 0;
        };

        [[nodiscard]] const Comet::InputOverrides::Action* action_patch(Comet::Uuid id) const;
        [[nodiscard]] Comet::InputOverrides::Binding binding_patch(
            Comet::Uuid action, Comet::Uuid binding) const;
        void commit(std::vector<Comet::InputOverrides::Action> actions);
        void restore_action(Comet::Uuid id);
        void restore_binding(Comet::Uuid action, Comet::Uuid binding);
        void disable_action(const Action& action, bool disabled);
        void disable_binding(const Action& action, const Binding& binding, bool disabled);
        void commit_binding(
            const Action& action, const Binding& binding, Comet::InputOverrides::Binding patch);
        void store_binding(
            const Action& action, const Binding& binding, Comet::InputOverrides::Binding patch);
        void change_control(
            const Action& action, const Binding& binding, Comet::InputActions::Control control);
        void start_capture(const Action& action, const Binding& binding,
            const Comet::Input::Frame& input, bool gamepad_button);
        void capture_input(const Comet::Input::Frame& input);
        void render_action_selector(const Text& translations);
        void render_actions(const Comet::Input::Frame& input, const Text& translations);
        void render_binding(const Action& action, const Binding& binding,
            Comet::InputOverrides::Resolution& resolved, const Comet::Input::Frame& input,
            const Text& translations);
        void render_controls(const Action& action, const Binding& binding, const Binding& effective,
            const Comet::Input::Frame& input, const Text& translations);
        void render_feedback(const Text& translations);
        void render_diagnostics(
            const Comet::InputOverrides::Resolution& resolved, const Text& translations);
        void apply();

        Comet::InputActions m_defaults;
        Comet::InputOverrides m_draft;
        std::vector<Comet::Input::Key> m_reserved_keys;
        std::optional<Comet::InputOverrides> m_request;
        std::optional<Capture> m_capture;
        std::size_t m_selected_action = 0;
        std::string m_action_filter;
        std::string m_error;
        bool m_open = false;
        bool m_open_requested = false;
        bool m_close_requested = false;
        bool m_waiting = false;
    };
}
