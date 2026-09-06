#pragma once
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include "common/export.h"
#include "config/config.h"
#include "core/math_utils.h"

namespace Comet {
    class COMET_API Window {
    public:
        // 在主线程创建／销毁；平台初始化由实现管理，外部不可调用 glfwTerminate。
        explicit Window(const Config::Window& config);

        ~Window();
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        [[nodiscard]] GLFWwindow* get() const { return m_window; }

        [[nodiscard]] bool should_close() const;

        [[nodiscard]] Math::Vec2u get_framebuffer_size() const;

        void poll_events();

        void wait_events();

    private:
        GLFWwindow* m_window = nullptr;
    };
}
