#pragma once

#include "common/export.h"
#include "config/config.h"
#include "core/math_utils.h"
#include <filesystem>
#include <memory>
#include <vector>

struct GLFWwindow;

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

        [[nodiscard]] GLFWwindow* get() const { return m_window.get(); }

        [[nodiscard]] bool should_close() const;
        void request_close();
        [[nodiscard]] bool is_minimized() const;

        [[nodiscard]] Math::Vec2u get_framebuffer_size() const;

        void poll_events();

        void wait_events();
        [[nodiscard]] std::vector<FileDrop> take_file_drops();

    private:
        struct WindowDeleter {
            void operator()(GLFWwindow* window) const noexcept;
        };

        std::unique_ptr<GLFWwindow, WindowDeleter> m_window;
        std::vector<FileDrop> m_file_drops;
    };
}
