#pragma once

#include "common/export.h"
#include "config/config.h"
#include "core/math_utils.h"
#include "core/input.h"
#include <filesystem>
#include <memory>
#include <string>
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

        // user pointer 归 Window；替换原生输入回调时必须串接原回调。
        [[nodiscard]] GLFWwindow* get() const { return m_window.get(); }

        [[nodiscard]] std::string get_title() const;
        void set_title(const std::string& title);

        [[nodiscard]] bool should_close() const;
        void request_close();
        void confirm_close_requests(bool enabled) { m_confirm_close = enabled; }
        [[nodiscard]] bool take_close_request();
        [[nodiscard]] bool is_minimized() const;

        [[nodiscard]] Math::Vec2u get_framebuffer_size() const;

        void poll_events();
        // 事件采集不推进快照；由 Engine 在 Update 前发布一次。
        const Input::Frame& publish_input_frame();
        void discard_pending_input() { m_input.discard_pending(); }
        [[nodiscard]] const Input::Frame& get_input_frame() const { return m_input.get_frame(); }

        void wait_events();
        void wait_events(double timeout_seconds);
        [[nodiscard]] std::vector<FileDrop> take_file_drops();

    private:
        void install_input_callbacks();

        struct WindowDeleter {
            void operator()(GLFWwindow* window) const noexcept;
        };

        Input m_input;
        std::unique_ptr<GLFWwindow, WindowDeleter> m_window;
        std::vector<FileDrop> m_file_drops;
        bool m_confirm_close = false;
        bool m_close_requested = false;
    };
}
