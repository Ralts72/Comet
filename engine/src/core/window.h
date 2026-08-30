#pragma once
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include "config/config.h"
#include "core/math_utils.h"

namespace Comet {
    class Window {
    public:
        explicit Window(const Config::Window& config);

        ~Window();

        [[nodiscard]] GLFWwindow* get() const { return m_window; }

        [[nodiscard]] bool should_close() const;

        [[nodiscard]] Math::Vec2u get_framebuffer_size() const;

        void poll_events();

        void wait_events();

    private:
        GLFWwindow* m_window = nullptr;
    };
}
