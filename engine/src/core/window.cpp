#include "window.h"
#include "config/config.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstddef>
#include <exception>
#include <string_view>
#include <utility>

namespace Comet {
    namespace {
        // GLFW window creation and destruction must run on the main thread.
        std::size_t window_count = 0;
    }

    void Window::WindowDeleter::operator()(GLFWwindow* window) const noexcept {
        glfwDestroyWindow(window);
        if(--window_count == 0)
            glfwTerminate();
    }

    Window::Window(const Config::Window& config) {
        PROFILE_SCOPE("Window::Constructor");
        if(window_count == 0 && glfwInit() != GLFW_TRUE)
            LOG_FATAL("Failed to initialize GLFW.");

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
            }
        }

        m_window.reset(glfwCreateWindow(
            actual_width, actual_height, config.title.c_str(), monitor, nullptr));
        if(!m_window) {
            if(window_count == 0)
                glfwTerminate();
            LOG_FATAL("Failed to create glfw window.");
        }
        ++window_count;
        glfwSetWindowUserPointer(m_window.get(), this);
        glfwSetDropCallback(
            m_window.get(), [](GLFWwindow* window, int count, const char** paths) {
                try {
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
                } catch(const std::exception& error) {
                    LOG_ERROR("Cannot receive dropped files: {}", error.what());
                }
            });

        // 窗口模式下居中显示，全屏模式不需要
        if(!config.fullscreen) {
            if(GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor()) {
                int x_pos, y_pos, work_width, work_height;
                glfwGetMonitorWorkarea(
                    primary_monitor, &x_pos, &y_pos, &work_width, &work_height);
                glfwSetWindowPos(m_window.get(), work_width / 2 - config.width / 2,
                    work_height / 2 - config.height / 2);
            }
        }

        glfwShowWindow(m_window.get());
    }

    Window::~Window() = default;

    bool Window::should_close() const {
        return glfwWindowShouldClose(m_window.get());
    }

    void Window::request_close() {
        glfwSetWindowShouldClose(m_window.get(), GLFW_TRUE);
    }

    bool Window::is_minimized() const {
        return glfwGetWindowAttrib(m_window.get(), GLFW_ICONIFIED) == GLFW_TRUE;
    }

    Math::Vec2u Window::get_framebuffer_size() const {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_window.get(), &width, &height);
        return {static_cast<uint32_t>(std::max(width, 0)),
            static_cast<uint32_t>(std::max(height, 0))};
    }

    void Window::poll_events() {
        PROFILE_SCOPE("Window::PollEvents");
        glfwPollEvents();
    }

    void Window::wait_events() {
        PROFILE_SCOPE("Window::WaitEvents");
        glfwWaitEvents();
    }

    std::vector<Window::FileDrop> Window::take_file_drops() {
        return std::exchange(m_file_drops, {});
    }

}
