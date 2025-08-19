#include "window.h"
#include "core/logger/logger.h"
#include "common/profiler.h"
#define GL_FALSE 0

namespace Comet {
    Window::Window(const std::string& title, const int width, const int height) : m_title(title), m_width(width), m_height(height) {
        PROFILE_SCOPE("Window::Constructor");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GL_FALSE);
        m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
        if(!m_window) {
            LOG_ERROR("Failed to create glfw window.");
            return;
        }

        if(GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor()) {
            int x_pos, y_pos, work_width, work_height;
            glfwGetMonitorWorkarea(primary_monitor, &x_pos, &y_pos, &work_width, &work_height);
            glfwSetWindowPos(m_window, work_width / 2 - width / 2, work_height / 2 - height / 2);
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
