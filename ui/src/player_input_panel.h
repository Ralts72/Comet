#pragma once

#include "input/player_input_edit.h"

#include <map>
#include <optional>
#include <span>
#include <string>

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
        Comet::PlayerInputEdit m_edit;
        std::size_t m_selected_action = 0;
        std::string m_action_filter;
        bool m_open = false;
        bool m_open_requested = false;
        bool m_close_requested = false;
    };
}
