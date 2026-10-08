#pragma once

#include "common/export.h"
#include "core/window_settings.h"
#include "core/math_utils.h"
#include "input/input.h"
#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Comet {
    class COMET_API Window {
    public:
        struct FileDrop {
            std::vector<std::filesystem::path> paths;
            Math::Vec2 position{};
        };

        struct UiEvent {
            enum class Type {
                KeyDown,
                KeyUp,
                Text,
                MouseMove,
                MouseDown,
                MouseUp,
                Scroll,
                Focus,
                PointerLeave
            };
            enum Modifier : uint8_t {
                Shift = 1,
                Control = 2,
                Alt = 4,
                Super = 8,
                CapsLock = 16,
                NumLock = 32
            };

            Type type{};
            Input::Key key = Input::Key::Unknown;
            Input::MouseButton button = Input::MouseButton::Left;
            uint8_t modifiers = 0;
            char32_t codepoint = 0;
            // 光标使用窗口逻辑坐标；Scroll 使用滚轮偏移。
            Math::Vec2 position{};
            bool repeat = false;
            bool focused = false;
        };
        static constexpr size_t MAX_UI_EVENTS = 512;

        explicit Window(const WindowSettings& config);

        ~Window();
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        // user pointer 归 Window；替换原生输入回调时必须串接原回调。
        [[nodiscard]] GLFWwindow* get() const { return m_window.get(); }

        [[nodiscard]] std::string get_title() const;
        void set_title(const std::string& title);
        void set_clipboard_text(const std::string& text);
        [[nodiscard]] std::string get_clipboard_text() const;

        [[nodiscard]] bool should_close() const;
        void request_close();
        void confirm_close_requests(bool enabled) { m_confirm_close = enabled; }
        [[nodiscard]] bool take_close_request();
        [[nodiscard]] bool is_minimized() const;

        // 仅前台非最小化窗口可锁定；失焦后不自动恢复。
        void set_cursor_locked(bool locked);
        [[nodiscard]] bool is_cursor_locked() const;
        [[nodiscard]] Math::Vec2 get_cursor_position() const;

        [[nodiscard]] Math::Vec2u get_framebuffer_size() const;
        [[nodiscard]] Math::Vec2u get_size() const;
        [[nodiscard]] Math::Vec2 get_content_scale() const;

        void poll_events();
        // 事件采集不推进快照；由 Engine 在 Update 前发布一次。
        const Input::Frame& publish_input_frame();
        void discard_pending_input();
        [[nodiscard]] const Input::Frame& get_input_frame() const { return m_input.get_frame(); }
        // 与物理快照同时发布，保持有效直到下次 publish_input_frame；不会消费事件。
        [[nodiscard]] std::span<const UiEvent> get_ui_events() const {
            return {m_ui_events.data(), m_ui_event_count};
        }

        void wait_events();
        void wait_events(double timeout_seconds);
        [[nodiscard]] std::vector<FileDrop> take_file_drops();

    private:
        void install_input_callbacks();
        void append_ui_event(const UiEvent& event);

        struct WindowDeleter {
            void operator()(GLFWwindow* window) const noexcept;
        };

        Input m_input;
        std::unique_ptr<GLFWwindow, WindowDeleter> m_window;
        std::vector<FileDrop> m_file_drops;
        std::array<UiEvent, MAX_UI_EVENTS> m_pending_ui_events{};
        std::array<UiEvent, MAX_UI_EVENTS> m_ui_events{};
        size_t m_pending_ui_event_count = 0;
        size_t m_ui_event_count = 0;
        Math::Vec2 m_ui_cursor_position{};
        uint8_t m_ui_modifiers = 0;
        bool m_ui_focused = false;
        bool m_ui_events_overflowed = false;
        bool m_confirm_close = false;
        bool m_close_requested = false;
    };
}
