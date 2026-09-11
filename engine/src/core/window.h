#pragma once
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include "common/export.h"
#include "config/config.h"
#include "core/math_utils.h"
#include <filesystem>
#include <vector>

namespace Comet {
    class COMET_API Window {
    public:
        struct FileDrop {
            std::vector<std::filesystem::path> paths;
            Math::Vec2 position{};
        };

        explicit Window(const Config::Window& config);

        ~Window();
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        [[nodiscard]] GLFWwindow* get() const { return m_window; }

        [[nodiscard]] bool should_close() const;

        [[nodiscard]] Math::Vec2u get_framebuffer_size() const;

        void poll_events();

        void wait_events();
        [[nodiscard]] std::vector<FileDrop> take_file_drops();

    private:
        GLFWwindow* m_window = nullptr;
        std::vector<FileDrop> m_file_drops;
    };
}
