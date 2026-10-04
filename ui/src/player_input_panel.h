#pragma once

#include "input/input_overrides.h"

#include <map>
#include <optional>
#include <string>

namespace CometUi {
    class PlayerInputPanel final {
    public:
        using Text = std::map<std::string, std::string, std::less<>>;

        void open(const Comet::InputActions& defaults, const Comet::InputOverrides& current);
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
        };

        [[nodiscard]] const Comet::InputOverrides::Action* action_patch(Comet::Uuid id) const;
        [[nodiscard]] Comet::InputOverrides::Binding binding_patch(
            Comet::Uuid action, Comet::Uuid binding) const;
        void commit(std::vector<Comet::InputOverrides::Action> actions);
        void restore_action(Comet::Uuid id);
        void restore_binding(Comet::Uuid action, Comet::Uuid binding);
        void disable_action(const Action& action, bool disabled);
        void store_binding(
            const Action& action, const Binding& binding, Comet::InputOverrides::Binding patch);
        void change_control(
            const Action& action, const Binding& binding, Comet::InputActions::Control control);
        void capture_key(const Comet::Input::Frame& input);
        void render_actions(const Comet::Input::Frame& input, const Text& translations);
        void render_binding(const Action& action, const Binding& binding,
            const Comet::Input::Frame& input, const Text& translations);
        void render_controls(const Action& action, const Binding& binding,
            const Comet::InputOverrides::Binding& patch, const Comet::Input::Frame& input,
            const Text& translations);
        void render_feedback(const Text& translations) const;
        void render_diagnostics(
            const Comet::InputOverrides::Resolution& resolved, const Text& translations) const;
        void apply();

        Comet::InputActions m_defaults;
        Comet::InputOverrides m_draft;
        std::optional<Comet::InputOverrides> m_request;
        std::optional<Capture> m_capture;
        std::size_t m_selected_action = 0;
        std::string m_error;
        bool m_open = false;
        bool m_open_requested = false;
        bool m_close_requested = false;
        bool m_waiting = false;
    };
}
