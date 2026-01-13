#include "window.h"
#include "pch.h"
#include "common/logger.h"
#include "common/profiler.h"
#include "common/config.h"
#define GL_FALSE 0

namespace Comet {
    Window::Window(const std::string& title, const int width, const int height) : m_title(title), m_width(width), m_height(height) {
        PROFILE_SCOPE("Window::Constructor");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GL_FALSE);

        // 从配置读取窗口属性
        const bool resizable = Config::get<bool>("window.resizable", true);
        glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);

        const bool fullscreen = Config::get<bool>("window.fullscreen", false);
        GLFWmonitor* monitor = nullptr;
        int actual_width = width;
        int actual_height = height;

        if(fullscreen) {
            monitor = glfwGetPrimaryMonitor();
            if(monitor) {
                // 全屏模式下使用显示器的当前视频模式分辨率
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                actual_width = mode->width;
                actual_height = mode->height;
                LOG_INFO("Fullscreen mode: using monitor resolution {}x{}", actual_width, actual_height);
            }
        }

        m_window = glfwCreateWindow(actual_width, actual_height, title.c_str(), monitor, nullptr);
        if(!m_window) {
            LOG_ERROR("Failed to create glfw window.");
            return;
        }

        // 更新实际的窗口尺寸
        m_width = actual_width;
        m_height = actual_height;

        // 窗口模式下居中显示，全屏模式不需要
        if(!fullscreen) {
            if(GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor()) {
                int x_pos, y_pos, work_width, work_height;
                glfwGetMonitorWorkarea(primary_monitor, &x_pos, &y_pos, &work_width, &work_height);
                glfwSetWindowPos(m_window, work_width / 2 - width / 2, work_height / 2 - height / 2);
            }
        }

        glfwMakeContextCurrent(m_window);
        // SetupWindowCallbacks();
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

    void Window::poll_events() {
        PROFILE_SCOPE("Window::PollEvents");
        glfwPollEvents();
    }

    void Window::swap_buffers() {
        PROFILE_SCOPE("Window::SwapBuffers");
        glfwSwapBuffers(m_window);
    }
}
