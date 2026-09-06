#include "window.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace {
    // GLFW 是进程级平台状态；最后一个窗口关闭后仍保留，避免反复启动 Cocoa。
    class GlfwRuntime {
    public:
        GlfwRuntime() {
            if(glfwInit() != GLFW_TRUE) {
                const char* description = nullptr;
                glfwGetError(&description);
                throw std::runtime_error(description ? description : "GLFW init failed");
            }
        }
        ~GlfwRuntime() { glfwTerminate(); }
    };

    Comet::Input::Key input_key(const int key) {
        using Key = Comet::Input::Key;
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
            std::pair{GLFW_KEY_PERIOD, Key::Period},
            std::pair{GLFW_KEY_SLASH, Key::Slash},
            std::pair{GLFW_KEY_SEMICOLON, Key::Semicolon},
            std::pair{GLFW_KEY_EQUAL, Key::Equal},
            std::pair{GLFW_KEY_LEFT_BRACKET, Key::LeftBracket},
            std::pair{GLFW_KEY_BACKSLASH, Key::Backslash},
            std::pair{GLFW_KEY_RIGHT_BRACKET, Key::RightBracket},
            std::pair{GLFW_KEY_GRAVE_ACCENT, Key::GraveAccent},
            std::pair{GLFW_KEY_WORLD_1, Key::World1},
            std::pair{GLFW_KEY_WORLD_2, Key::World2},
            std::pair{GLFW_KEY_ESCAPE, Key::Escape},
            std::pair{GLFW_KEY_ENTER, Key::Enter}, std::pair{GLFW_KEY_TAB, Key::Tab},
            std::pair{GLFW_KEY_BACKSPACE, Key::Backspace},
            std::pair{GLFW_KEY_INSERT, Key::Insert},
            std::pair{GLFW_KEY_DELETE, Key::Delete},
            std::pair{GLFW_KEY_RIGHT, Key::Right}, std::pair{GLFW_KEY_LEFT, Key::Left},
            std::pair{GLFW_KEY_DOWN, Key::Down}, std::pair{GLFW_KEY_UP, Key::Up},
            std::pair{GLFW_KEY_PAGE_UP, Key::PageUp},
            std::pair{GLFW_KEY_PAGE_DOWN, Key::PageDown},
            std::pair{GLFW_KEY_HOME, Key::Home}, std::pair{GLFW_KEY_END, Key::End},
            std::pair{GLFW_KEY_CAPS_LOCK, Key::CapsLock},
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
}

namespace Comet {
    Window::Window(const Config::Window& config) {
        PROFILE_SCOPE("Window::Constructor");
        static const GlfwRuntime runtime;
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

        glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

        GLFWmonitor* monitor = nullptr;
        int actual_width = config.width;
        int actual_height = config.height;

        if(config.fullscreen) {
            monitor = glfwGetPrimaryMonitor();
            if(monitor) {
                // 全屏模式下使用显示器的当前视频模式分辨率
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                actual_width = mode->width;
                actual_height = mode->height;
                LOG_INFO("Fullscreen mode: using monitor resolution {}x{}", actual_width,
                    actual_height);
            }
        }

        m_window = glfwCreateWindow(
            actual_width, actual_height, config.title.c_str(), monitor, nullptr);
        if(!m_window) {
            LOG_FATAL("Failed to create glfw window.");
        }

        // user pointer 归 Window；ImGui 后端会串接这些回调，不覆盖 user pointer。
        glfwSetWindowUserPointer(m_window, this);
        glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int, int action,
                                         int) {
            if(action != GLFW_PRESS && action != GLFW_RELEASE)
                return;
            auto& input = static_cast<Window*>(glfwGetWindowUserPointer(window))->m_input;
            input.key_event(input_key(key), action == GLFW_PRESS);
        });
        glfwSetMouseButtonCallback(m_window, [](GLFWwindow* window, int button,
                                                 int action, int) {
            if(action != GLFW_PRESS && action != GLFW_RELEASE)
                return;
            auto& input = static_cast<Window*>(glfwGetWindowUserPointer(window))->m_input;
            input.mouse_button_event(
                static_cast<Input::MouseButton>(button), action == GLFW_PRESS);
        });
        glfwSetCursorPosCallback(m_window, [](GLFWwindow* window, double x, double y) {
            auto& input = static_cast<Window*>(glfwGetWindowUserPointer(window))->m_input;
            input.cursor_event({static_cast<float>(x), static_cast<float>(y)});
        });
        glfwSetScrollCallback(m_window, [](GLFWwindow* window, double x, double y) {
            auto& input = static_cast<Window*>(glfwGetWindowUserPointer(window))->m_input;
            input.scroll_event({static_cast<float>(x), static_cast<float>(y)});
        });
        glfwSetWindowFocusCallback(m_window, [](GLFWwindow* window, int focused) {
            auto& input = static_cast<Window*>(glfwGetWindowUserPointer(window))->m_input;
            input.focus_event(focused == GLFW_TRUE);
        });

        // 窗口模式下居中显示，全屏模式不需要
        if(!config.fullscreen) {
            if(GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor()) {
                int x_pos, y_pos, work_width, work_height;
                glfwGetMonitorWorkarea(
                    primary_monitor, &x_pos, &y_pos, &work_width, &work_height);
                glfwSetWindowPos(m_window, work_width / 2 - config.width / 2,
                    work_height / 2 - config.height / 2);
            }
        }

        glfwShowWindow(m_window);
        m_input.focus_event(glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE);
        double cursor_x = 0;
        double cursor_y = 0;
        glfwGetCursorPos(m_window, &cursor_x, &cursor_y);
        m_input.cursor_event(
            {static_cast<float>(cursor_x), static_cast<float>(cursor_y)});
    }

    Window::~Window() {
        glfwDestroyWindow(m_window);
        LOG_INFO("Window destroyed.");
    }

    bool Window::should_close() const {
        return glfwWindowShouldClose(m_window);
    }

    void Window::request_close() {
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    }

    Math::Vec2u Window::get_framebuffer_size() const {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        return {static_cast<uint32_t>(std::max(width, 0)),
            static_cast<uint32_t>(std::max(height, 0))};
    }

    void Window::poll_events() {
        PROFILE_SCOPE("Window::PollEvents");
        glfwPollEvents();
        static_assert(Input::MAX_GAMEPADS == GLFW_JOYSTICK_LAST + 1);
        static_assert(static_cast<size_t>(Input::GamepadButton::Count)
                      == GLFW_GAMEPAD_BUTTON_LAST + 1);
        static_assert(
            static_cast<size_t>(Input::GamepadAxis::Count) == GLFW_GAMEPAD_AXIS_LAST + 1);
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
        m_input.publish_frame();
    }

    void Window::wait_events() {
        PROFILE_SCOPE("Window::WaitEvents");
        glfwWaitEvents();
    }

}
