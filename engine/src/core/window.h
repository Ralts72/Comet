#pragma once
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include "common/config.h"

#include <string>

namespace Comet {
    class Window {
    public:
        explicit Window(const Config::Window& config);

        ~Window();

        [[nodiscard]] GLFWwindow* get() const { return m_window; }

        [[nodiscard]] bool should_close() const;

        void poll_events();

        void swap_buffers() const;

    private:
        GLFWwindow* m_window;
        std::string m_title;
        [[maybe_unused]] int m_width;
        [[maybe_unused]] int m_height;
    };
}
