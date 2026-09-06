#pragma once
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include "common/export.h"
#include "config/config.h"
#include "core/math_utils.h"
#include "core/input.h"

namespace Comet {
    class COMET_API Window {
    public:
        // 在主线程创建／销毁；平台初始化由实现管理，外部不可调用 glfwTerminate。
        explicit Window(const Config::Window& config);

        ~Window();
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        // 原生互操作借用句柄；user pointer 归 Window，替换输入回调须串接原回调。
        [[nodiscard]] GLFWwindow* get() const { return m_window; }

        [[nodiscard]] bool should_close() const;
        void request_close();
        [[nodiscard]] const Input::Frame& get_input_frame() const {
            return m_input.get_frame();
        }

        [[nodiscard]] Math::Vec2u get_framebuffer_size() const;

        void poll_events();

        void wait_events();

    private:
        GLFWwindow* m_window = nullptr;
        Input m_input;
    };
}
