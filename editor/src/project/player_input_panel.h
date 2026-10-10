#pragma once

#include "input/player_input_edit.h"

#include <optional>
#include <span>
#include <string>

namespace CometEditor {
    class PlayerInputPanel final {
    public:
        void open(const Comet::InputActions& defaults, const Comet::InputOverrides& current,
            std::span<const Comet::Input::Key> reserved_keys = {});
        [[nodiscard]] bool is_open() const { return m_open; }
        void close();
        // 关闭当帧也阻断游戏输入，宿主仍使用原有 Gate。
        [[nodiscard]] bool render(const Comet::Input::Frame& input);
        [[nodiscard]] std::optional<Comet::InputOverrides> take_request();
        void complete(const Comet::Result<void>& result);

    private:
        using Action = Comet::InputActions::Action;
        using Binding = Comet::InputActions::Binding;
        void render_action_selector();
        void render_actions(const Comet::Input::Frame& input);
        void render_binding(const Action& action, const Binding& binding,
            Comet::InputOverrides::Resolution& resolved, const Comet::Input::Frame& input);
        void render_controls(const Action& action, const Binding& binding, const Binding& effective,
            const Comet::Input::Frame& input);
        void render_capture_button(const Action& action, const Binding& binding,
            Comet::PlayerInputEdit::CaptureKind kind, const Comet::Input::Frame& input);
        void render_feedback();
        void render_diagnostics(const Comet::InputOverrides::Resolution& resolved);
        void render_error();
        void render_content(const Comet::Input::Frame& input);
        void render_footer();
        Comet::PlayerInputEdit m_edit;
        std::size_t m_selected_action = 0;
        std::string m_action_filter;
        bool m_open = false;
        bool m_open_requested = false;
        bool m_close_requested = false;
    };
}
