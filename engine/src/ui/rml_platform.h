#pragma once

#include "common/export.h"

#include "input/input.h"
#include "ui/view.h"

#include <bitset>
#include <chrono>
#include <optional>

namespace Rml {
    class Context;
}
namespace Comet {
    class Window;
}

namespace Comet::Ui {
    // 不替换 GLFW 回调；消费 Window 与物理快照同步发布的有序事件。
    class COMET_API RmlPlatform {
    public:
        void update(Rml::Context& context, const Comet::Window& window,
            const Comet::Input::Frame& frame, bool modal_open,
            const std::optional<View>& view = {});
        void set_capture_active(bool active) { m_capture_active = active; }
        [[nodiscard]] bool text_input_active() const { return m_text_input_active; }
        // 关闭弹层或切换改键录入时调用；也会停止当前 update 的剩余事件。
        void cancel_input(Rml::Context& context);
        void stop_dispatch_preserve_focus(Rml::Context& context);

    private:
        void release_input(Rml::Context& context, bool blur_focus);
        void refresh_text_input(Rml::Context& context);
        void update_gamepad(Rml::Context& context, const Comet::Input::Frame& frame);

        Comet::Input::Gate m_gate;
        Comet::Input::Gate m_pointer_gate;
        std::bitset<static_cast<size_t>(Comet::Input::Key::Count)> m_keys_down;
        std::bitset<static_cast<size_t>(Comet::Input::MouseButton::Count)> m_mouse_down;
        std::optional<uint64_t> m_serial;
        std::optional<size_t> m_gamepad;
        uint64_t m_interruption = 0;
        uint64_t m_connection_revision = 0;
        std::chrono::steady_clock::time_point m_next_navigation{};
        int m_navigation_direction = -1;
        bool m_navigation_blocked = true;
        bool m_modal_open = false;
        bool m_window_accepting = false;
        bool m_capture_active = false;
        bool m_previous_capture = false;
        bool m_text_input_active = false;
        bool m_cancelled = false;
        bool m_cancelling = false;
    };
}
