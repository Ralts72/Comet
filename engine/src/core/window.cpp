#include "window.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <limits>
#include <string_view>
#include <utility>

namespace Comet {
    namespace {
        Input::Key translate_key(const int key) {
            using Key = Input::Key;
            if(key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
                return static_cast<Key>(static_cast<int>(Key::A) + key - GLFW_KEY_A);
            if(key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
                return static_cast<Key>(static_cast<int>(Key::Digit0) + key - GLFW_KEY_0);
            if(key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25)
                return static_cast<Key>(static_cast<int>(Key::F1) + key - GLFW_KEY_F1);
            if(key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
                return static_cast<Key>(static_cast<int>(Key::Keypad0) + key - GLFW_KEY_KP_0);
            constexpr std::array special_keys{std::pair{GLFW_KEY_SPACE, Key::Space},
                std::pair{GLFW_KEY_APOSTROPHE, Key::Apostrophe},
                std::pair{GLFW_KEY_COMMA, Key::Comma}, std::pair{GLFW_KEY_MINUS, Key::Minus},
                std::pair{GLFW_KEY_PERIOD, Key::Period}, std::pair{GLFW_KEY_SLASH, Key::Slash},
                std::pair{GLFW_KEY_SEMICOLON, Key::Semicolon},
                std::pair{GLFW_KEY_EQUAL, Key::Equal},
                std::pair{GLFW_KEY_LEFT_BRACKET, Key::LeftBracket},
                std::pair{GLFW_KEY_BACKSLASH, Key::Backslash},
                std::pair{GLFW_KEY_RIGHT_BRACKET, Key::RightBracket},
                std::pair{GLFW_KEY_GRAVE_ACCENT, Key::GraveAccent},
                std::pair{GLFW_KEY_WORLD_1, Key::World1}, std::pair{GLFW_KEY_WORLD_2, Key::World2},
                std::pair{GLFW_KEY_ESCAPE, Key::Escape}, std::pair{GLFW_KEY_ENTER, Key::Enter},
                std::pair{GLFW_KEY_TAB, Key::Tab}, std::pair{GLFW_KEY_BACKSPACE, Key::Backspace},
                std::pair{GLFW_KEY_INSERT, Key::Insert}, std::pair{GLFW_KEY_DELETE, Key::Delete},
                std::pair{GLFW_KEY_RIGHT, Key::Right}, std::pair{GLFW_KEY_LEFT, Key::Left},
                std::pair{GLFW_KEY_DOWN, Key::Down}, std::pair{GLFW_KEY_UP, Key::Up},
                std::pair{GLFW_KEY_PAGE_UP, Key::PageUp},
                std::pair{GLFW_KEY_PAGE_DOWN, Key::PageDown}, std::pair{GLFW_KEY_HOME, Key::Home},
                std::pair{GLFW_KEY_END, Key::End}, std::pair{GLFW_KEY_CAPS_LOCK, Key::CapsLock},
                std::pair{GLFW_KEY_SCROLL_LOCK, Key::ScrollLock},
                std::pair{GLFW_KEY_NUM_LOCK, Key::NumLock},
                std::pair{GLFW_KEY_PRINT_SCREEN, Key::PrintScreen},
                std::pair{GLFW_KEY_PAUSE, Key::Pause},
                std::pair{GLFW_KEY_KP_DECIMAL, Key::KeypadDecimal},
                std::pair{GLFW_KEY_KP_DIVIDE, Key::KeypadDivide},
                std::pair{GLFW_KEY_KP_MULTIPLY, Key::KeypadMultiply},
                std::pair{GLFW_KEY_KP_SUBTRACT, Key::KeypadSubtract},
                std::pair{GLFW_KEY_KP_ADD, Key::KeypadAdd},
                std::pair{GLFW_KEY_KP_ENTER, Key::KeypadEnter},
                std::pair{GLFW_KEY_KP_EQUAL, Key::KeypadEqual},
                std::pair{GLFW_KEY_LEFT_SHIFT, Key::LeftShift},
                std::pair{GLFW_KEY_LEFT_CONTROL, Key::LeftControl},
                std::pair{GLFW_KEY_LEFT_ALT, Key::LeftAlt},
                std::pair{GLFW_KEY_LEFT_SUPER, Key::LeftSuper},
                std::pair{GLFW_KEY_RIGHT_SHIFT, Key::RightShift},
                std::pair{GLFW_KEY_RIGHT_CONTROL, Key::RightControl},
                std::pair{GLFW_KEY_RIGHT_ALT, Key::RightAlt},
                std::pair{GLFW_KEY_RIGHT_SUPER, Key::RightSuper},
                std::pair{GLFW_KEY_MENU, Key::Menu}};
            for(const auto& [native, translated] : special_keys)
                if(key == native)
                    return translated;
            return Key::Unknown;
        }

        uint8_t translate_modifiers(const int modifiers) {
            uint8_t result = 0;
            if(modifiers & GLFW_MOD_SHIFT)
                result |= Window::UiEvent::Shift;
            if(modifiers & GLFW_MOD_CONTROL)
                result |= Window::UiEvent::Control;
            if(modifiers & GLFW_MOD_ALT)
                result |= Window::UiEvent::Alt;
            if(modifiers & GLFW_MOD_SUPER)
                result |= Window::UiEvent::Super;
            if(modifiers & GLFW_MOD_CAPS_LOCK)
                result |= Window::UiEvent::CapsLock;
            if(modifiers & GLFW_MOD_NUM_LOCK)
                result |= Window::UiEvent::NumLock;
            return result;
        }

        // GLFW 窗口的创建和销毁必须在主线程执行。
        std::size_t window_count = 0;
    }

    void Window::WindowDeleter::operator()(GLFWwindow* window) const noexcept {
        glfwDestroyWindow(window);
        if(--window_count == 0)
            glfwTerminate();
    }

    Window::Window(const WindowSettings& config) {
        PROFILE_SCOPE("Window::Constructor");
        m_mode = config.mode;
        if(window_count == 0 && glfwInit() != GLFW_TRUE)
            LOG_FATAL("Failed to initialize GLFW.");

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

        glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_MAXIMIZED,
            config.maximized && config.mode == WindowMode::Windowed ? GLFW_TRUE : GLFW_FALSE);
        m_restore_size = {
            static_cast<uint32_t>(config.width), static_cast<uint32_t>(config.height)};

        GLFWmonitor* monitor = nullptr;
        int actual_width = config.width;
        int actual_height = config.height;

        if(config.mode == WindowMode::Fullscreen) {
            monitor = glfwGetPrimaryMonitor();
            if(monitor) {
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                actual_width = mode->width;
                actual_height = mode->height;
            }
        }

        m_window.reset(
            glfwCreateWindow(actual_width, actual_height, config.title.c_str(), monitor, nullptr));
        if(!m_window) {
            if(window_count == 0)
                glfwTerminate();
            LOG_FATAL("Failed to create glfw window.");
        }
        ++window_count;
        glfwSetWindowUserPointer(m_window.get(), this);
        glfwSetWindowMaximizeCallback(m_window.get(), [](GLFWwindow* window, int maximized) {
            auto& owner = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            if(!owner.is_minimized())
                owner.m_restore_maximized = maximized == GLFW_TRUE;
        });
        glfwSetInputMode(m_window.get(), GLFW_LOCK_KEY_MODS, GLFW_TRUE);
        install_input_callbacks();
        glfwSetWindowCloseCallback(m_window.get(), [](GLFWwindow* window) {
            auto& owner = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            owner.set_cursor_locked(false);
            if(owner.m_confirm_close) {
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                owner.m_close_requested = true;
            }
        });
        glfwSetDropCallback(
            m_window.get(), [](GLFWwindow* window, int count, const char** paths) noexcept {
                FileDrop drop;
                double x, y;
                glfwGetCursorPos(window, &x, &y);
                drop.position = {static_cast<float>(x), static_cast<float>(y)};
                for(int i = 0; i < count; ++i) {
                    const std::string_view utf8(paths[i]);
                    drop.paths.emplace_back(std::u8string(utf8.begin(), utf8.end()));
                }
                static_cast<Window*>(glfwGetWindowUserPointer(window))
                    ->m_file_drops.push_back(std::move(drop));
            });

        if(config.mode == WindowMode::Windowed && !config.maximized) {
            if(GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor()) {
                int x_pos, y_pos, work_width, work_height;
                glfwGetMonitorWorkarea(primary_monitor, &x_pos, &y_pos, &work_width, &work_height);
                glfwSetWindowPos(m_window.get(), work_width / 2 - config.width / 2,
                    work_height / 2 - config.height / 2);
            }
        }

        glfwGetWindowPos(m_window.get(), &m_windowed_position[0], &m_windowed_position[1]);
        if(config.mode == WindowMode::Borderless) {
            m_mode = WindowMode::Windowed;
            if(auto changed = set_display_settings(WindowMode::Borderless, m_restore_size);
                !changed)
                LOG_FATAL("Cannot initialize borderless window: {}", changed.error());
        }
        glfwShowWindow(m_window.get());
        update_restore_state();
        m_ui_focused = glfwGetWindowAttrib(m_window.get(), GLFW_FOCUSED) == GLFW_TRUE;
        m_input.focus_event(m_ui_focused);
        double cursor_x = 0;
        double cursor_y = 0;
        glfwGetCursorPos(m_window.get(), &cursor_x, &cursor_y);
        m_ui_cursor_position = {static_cast<float>(cursor_x), static_cast<float>(cursor_y)};
        m_input.cursor_event(m_ui_cursor_position);
    }

    Window::~Window() = default;

    std::string Window::get_title() const {
        return glfwGetWindowTitle(m_window.get());
    }

    void Window::set_title(const std::string& title) {
        glfwSetWindowTitle(m_window.get(), title.c_str());
    }

    void Window::set_clipboard_text(const std::string& text) {
        glfwSetClipboardString(m_window.get(), text.c_str());
    }

    std::string Window::get_clipboard_text() const {
        const auto* text = glfwGetClipboardString(m_window.get());
        return text ? text : "";
    }

    bool Window::should_close() const {
        return glfwWindowShouldClose(m_window.get());
    }

    void Window::request_close() {
        set_cursor_locked(false);
        glfwSetWindowShouldClose(m_window.get(), GLFW_TRUE);
    }

    bool Window::take_close_request() {
        return std::exchange(m_close_requested, false);
    }

    bool Window::is_minimized() const {
        return glfwGetWindowAttrib(m_window.get(), GLFW_ICONIFIED) == GLFW_TRUE;
    }

    bool Window::is_maximized() const {
        if(is_minimized())
            return m_restore_maximized;
        return glfwGetWindowAttrib(m_window.get(), GLFW_MAXIMIZED) == GLFW_TRUE;
    }

    void Window::update_restore_state() {
        if(is_minimized() || m_mode != WindowMode::Windowed)
            return;
        m_restore_maximized = glfwGetWindowAttrib(m_window.get(), GLFW_MAXIMIZED) == GLFW_TRUE;
        if(m_restore_maximized)
            return;
        const auto size = get_size();
        if(size.x > 0 && size.y > 0)
            m_restore_size = size;
    }

    Result<void> Window::set_display_settings(WindowMode mode, Math::Vec2u windowed_size) {
        if(windowed_size.x == 0 || windowed_size.y == 0
            || windowed_size.x > static_cast<uint32_t>(std::numeric_limits<int>::max())
            || windowed_size.y > static_cast<uint32_t>(std::numeric_limits<int>::max()))
            return Result<void>::failure("Window dimensions are out of range");
        if(mode != WindowMode::Windowed && mode != WindowMode::Borderless
            && mode != WindowMode::Fullscreen)
            return Result<void>::failure("Unknown window mode");
        GLFWmonitor* monitor = nullptr;
        const GLFWvidmode* video = nullptr;
        if(mode != WindowMode::Windowed) {
            monitor = glfwGetPrimaryMonitor();
            if(monitor)
                video = glfwGetVideoMode(monitor);
            if(!video)
                return Result<void>::failure("No display mode is available");
        }
        if(mode == m_mode && windowed_size == m_restore_size)
            return Result<void>::success();
        if(m_mode == WindowMode::Windowed && !is_maximized() && !is_minimized())
            glfwGetWindowPos(m_window.get(), &m_windowed_position[0], &m_windowed_position[1]);
        if(is_maximized() || is_minimized())
            glfwRestoreWindow(m_window.get());
        m_mode = mode;
        m_restore_size = windowed_size;
        m_restore_maximized = false;
        set_cursor_locked(false);
        if(mode == WindowMode::Windowed) {
            glfwSetWindowAttrib(m_window.get(), GLFW_DECORATED, GLFW_TRUE);
            glfwSetWindowMonitor(m_window.get(), nullptr, m_windowed_position[0],
                m_windowed_position[1], static_cast<int>(windowed_size.x),
                static_cast<int>(windowed_size.y), GLFW_DONT_CARE);
        } else {
            int x = 0, y = 0;
            glfwGetMonitorPos(monitor, &x, &y);
            glfwSetWindowAttrib(m_window.get(), GLFW_DECORATED,
                mode == WindowMode::Borderless ? GLFW_FALSE : GLFW_TRUE);
            glfwSetWindowMonitor(m_window.get(), mode == WindowMode::Fullscreen ? monitor : nullptr,
                x, y, video->width, video->height, video->refreshRate);
        }
        return Result<void>::success();
    }

    void Window::set_cursor_locked(bool locked) {
        if(locked
            && (is_minimized() || glfwGetWindowAttrib(m_window.get(), GLFW_FOCUSED) != GLFW_TRUE))
            locked = false;
        if(is_cursor_locked() == locked)
            return;
        glfwSetInputMode(
            m_window.get(), GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if(glfwRawMouseMotionSupported() == GLFW_TRUE)
            glfwSetInputMode(
                m_window.get(), GLFW_RAW_MOUSE_MOTION, locked ? GLFW_TRUE : GLFW_FALSE);
        m_input.reset_cursor_baseline();
    }

    bool Window::is_cursor_locked() const {
        return glfwGetInputMode(m_window.get(), GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    }

    Math::Vec2 Window::get_cursor_position() const {
        double x = 0;
        double y = 0;
        glfwGetCursorPos(m_window.get(), &x, &y);
        return {static_cast<float>(x), static_cast<float>(y)};
    }

    Math::Vec2u Window::get_framebuffer_size() const {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_window.get(), &width, &height);
        return {
            static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0))};
    }

    Math::Vec2u Window::get_size() const {
        int width = 0;
        int height = 0;
        glfwGetWindowSize(m_window.get(), &width, &height);
        return {
            static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0))};
    }

    Math::Vec2 Window::get_content_scale() const {
        float x = 1;
        float y = 1;
        glfwGetWindowContentScale(m_window.get(), &x, &y);
        const Math::Vec2 scale{x, y};
        return Math::is_finite(scale) && x > 0 && y > 0 ? scale : Math::Vec2{1, 1};
    }

    void Window::append_ui_event(const UiEvent& event) {
        if(m_ui_events_overflowed)
            return;
        if(m_pending_ui_event_count == MAX_UI_EVENTS) {
            // 缺失任何一个 release 都会破坏状态；整批取消，禁止转发残缺序列。
            m_pending_ui_event_count = 0;
            m_ui_events_overflowed = true;
            m_input.discard_pending();
            return;
        }
        m_pending_ui_events[m_pending_ui_event_count++] = event;
    }

    void Window::discard_pending_input() {
        m_input.discard_pending();
        m_pending_ui_event_count = 0;
        m_ui_events_overflowed = false;
    }

    void Window::poll_events() {
        PROFILE_SCOPE("Window::PollEvents");
        // 原生最大化动画的 resize 回调含过渡尺寸，只在事件泵边界采样。
        update_restore_state();
        glfwPollEvents();
        update_restore_state();
    }

    void Window::install_input_callbacks() {
        glfwSetKeyCallback(m_window.get(), [](GLFWwindow* window, int key, int, int action,
                                               int mods) {
            if(action != GLFW_PRESS && action != GLFW_RELEASE && action != GLFW_REPEAT)
                return;
            auto& owner = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            const auto translated = translate_key(key);
            if(action != GLFW_REPEAT)
                owner.m_input.key_event(translated, action == GLFW_PRESS);
            owner.m_ui_modifiers = translate_modifiers(mods);
            if(owner.m_ui_focused && translated != Input::Key::Unknown)
                owner.append_ui_event(
                    {.type = action == GLFW_RELEASE ? UiEvent::Type::KeyUp : UiEvent::Type::KeyDown,
                        .key = translated,
                        .modifiers = owner.m_ui_modifiers,
                        .repeat = action == GLFW_REPEAT});
        });
        glfwSetCharCallback(m_window.get(), [](GLFWwindow* window, unsigned int codepoint) {
            auto& owner = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            const bool scalar = codepoint >= 32 && codepoint <= 0x10ffff
                                && !(codepoint >= 0xd800 && codepoint <= 0xdfff)
                                && codepoint != 127;
            if(owner.m_ui_focused && scalar)
                owner.append_ui_event({.type = UiEvent::Type::Text,
                    .modifiers = owner.m_ui_modifiers,
                    .codepoint = static_cast<char32_t>(codepoint)});
        });
        glfwSetMouseButtonCallback(
            m_window.get(), [](GLFWwindow* window, int button, int action, int mods) {
                if(action != GLFW_PRESS && action != GLFW_RELEASE)
                    return;
                auto& owner = *static_cast<Window*>(glfwGetWindowUserPointer(window));
                if(button < 0 || button >= static_cast<int>(Input::MouseButton::Count))
                    return;
                const auto translated = static_cast<Input::MouseButton>(button);
                owner.m_input.mouse_button_event(translated, action == GLFW_PRESS);
                owner.m_ui_modifiers = translate_modifiers(mods);
                if(owner.m_ui_focused) {
                    // 点击沿用此前移动事件的坐标，避免采样到尚未交付的新位置。
                    owner.append_ui_event({.type = action == GLFW_PRESS ? UiEvent::Type::MouseDown
                                                                        : UiEvent::Type::MouseUp,
                        .button = translated,
                        .modifiers = owner.m_ui_modifiers,
                        .position = owner.m_ui_cursor_position});
                }
            });
        glfwSetCursorPosCallback(m_window.get(), [](GLFWwindow* window, double x, double y) {
            auto& owner = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            const Math::Vec2 position{static_cast<float>(x), static_cast<float>(y)};
            if(!Math::is_finite(position))
                return;
            owner.m_ui_cursor_position = position;
            owner.m_input.cursor_event(position);
            if(owner.m_ui_focused)
                owner.append_ui_event({.type = UiEvent::Type::MouseMove,
                    .modifiers = owner.m_ui_modifiers,
                    .position = position});
        });
        glfwSetScrollCallback(m_window.get(), [](GLFWwindow* window, double x, double y) {
            auto& owner = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            const Math::Vec2 offset{static_cast<float>(x), static_cast<float>(y)};
            owner.m_input.scroll_event(offset);
            if(owner.m_ui_focused && Math::is_finite(offset))
                owner.append_ui_event({.type = UiEvent::Type::Scroll,
                    .modifiers = owner.m_ui_modifiers,
                    .position = offset});
        });
        glfwSetCursorEnterCallback(m_window.get(), [](GLFWwindow* window, int entered) {
            auto& owner = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            if(entered != GLFW_TRUE)
                owner.append_ui_event({.type = UiEvent::Type::PointerLeave});
        });
        glfwSetWindowFocusCallback(m_window.get(), [](GLFWwindow* window, int focused) {
            auto& owner = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            owner.m_input.focus_event(focused == GLFW_TRUE);
            if(owner.m_ui_focused != (focused == GLFW_TRUE)) {
                owner.m_ui_focused = focused == GLFW_TRUE;
                if(owner.m_ui_focused)
                    owner.m_ui_cursor_position = owner.get_cursor_position();
                owner.append_ui_event(
                    {.type = UiEvent::Type::Focus, .focused = owner.m_ui_focused});
            }
            if(focused != GLFW_TRUE) {
                owner.m_ui_modifiers = 0;
                owner.set_cursor_locked(false);
            }
        });
    }

    const Input::Frame& Window::publish_input_frame() {
        static_assert(Input::MAX_GAMEPADS == GLFW_JOYSTICK_LAST + 1);
        static_assert(
            static_cast<size_t>(Input::GamepadButton::Count) == GLFW_GAMEPAD_BUTTON_LAST + 1);
        static_assert(static_cast<size_t>(Input::GamepadAxis::Count) == GLFW_GAMEPAD_AXIS_LAST + 1);
        for(int index = GLFW_JOYSTICK_1; index <= GLFW_JOYSTICK_LAST; ++index) {
            GLFWgamepadstate native{};
            if(glfwGetGamepadState(index, &native) != GLFW_TRUE) {
                m_input.gamepad_sample(static_cast<size_t>(index), std::nullopt);
                continue;
            }
            Input::GamepadSample sample;
            for(size_t button = 0; button < sample.buttons.size(); ++button)
                sample.buttons[button] = native.buttons[button] == GLFW_PRESS;
            for(size_t axis = 0; axis < sample.axes.size(); ++axis) {
                sample.axes[axis] = native.axes[axis];
                if(axis >= GLFW_GAMEPAD_AXIS_LEFT_TRIGGER)
                    sample.axes[axis] = (sample.axes[axis] + 1.0f) * 0.5f;
            }
            m_input.gamepad_sample(static_cast<size_t>(index), sample);
        }
        const auto& frame = m_input.publish_frame();
        m_ui_event_count = m_pending_ui_event_count;
        std::copy_n(m_pending_ui_events.begin(), m_ui_event_count, m_ui_events.begin());
        m_pending_ui_event_count = 0;
        m_ui_events_overflowed = false;
        return frame;
    }

    void Window::wait_events(double timeout_seconds) {
        update_restore_state();
        glfwWaitEventsTimeout(timeout_seconds);
        update_restore_state();
    }

    void Window::wait_events() {
        PROFILE_SCOPE("Window::WaitEvents");
        update_restore_state();
        glfwWaitEvents();
        update_restore_state();
    }

    std::vector<Window::FileDrop> Window::take_file_drops() {
        return std::exchange(m_file_drops, {});
    }

}
