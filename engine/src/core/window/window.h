#pragma once
#include <GLFW/glfw3.h>
#include <common/math_utils.h>
#include <string>

namespace Comet {
    class Window {
    public:
        Window(const std::string& title, int width, int height);

        ~Window();

        [[nodiscard]] GLFWwindow* get_window() const { return m_window; }

        [[nodiscard]] Vec2i get_size() const;

        [[nodiscard]] bool should_close() const;

        void poll_events();

        void swap_buffers();

    private:
        GLFWwindow* m_window;
        std::string m_title;
        int m_width;
        int m_height;
    };
}
