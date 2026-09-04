#include "window.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include <algorithm>

namespace Comet {
    Window::Window(const Config::Window& config) {
        PROFILE_SCOPE("Window::Constructor");
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
    }

    Window::~Window() {
        glfwDestroyWindow(m_window);
        glfwTerminate();
        LOG_INFO("The window has been destroy.");
    }

    bool Window::should_close() const {
        return glfwWindowShouldClose(m_window);
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
    }

    void Window::wait_events() {
        PROFILE_SCOPE("Window::WaitEvents");
        glfwWaitEvents();
    }

}
