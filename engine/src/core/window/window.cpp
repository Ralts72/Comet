#include "window.h"
#include "common/log_system/log_system.h"
#define GL_FALSE 0

namespace Comet {
    Window::Window(const std::string& title, const int width, const int height) : m_title(title), m_width(width), m_height(height) {
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

    Vec2i Window::get_size() const {
        return {m_width, m_height};
    }

    bool Window::should_close() const {
        return glfwWindowShouldClose(m_window);
    }

    void Window::poll_events() {
        glfwPollEvents();
    }

    void Window::swap_buffers() {
        glfwSwapBuffers(m_window);
    }
}
