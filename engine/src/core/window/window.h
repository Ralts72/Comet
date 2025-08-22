#pragma once
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "pch.h"

namespace Comet {
    class Window {
    public:
        Window(const std::string& title, int width, int height);

        ~Window();

        [[nodiscard]] GLFWwindow* get() const { return m_window; }

        [[nodiscard]] bool should_close() const;

        void poll_events();

        void swap_buffers();

    private:
        GLFWwindow* m_window;
        std::string m_title;
        [[maybe_unused]] int m_width;
        [[maybe_unused]] int m_height;
    };
}
