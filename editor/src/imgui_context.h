#pragma once
#include "graphics/vk_common.h"
#include <functional>
#include <memory>
#include <vector>

namespace Comet {
    class RenderContext;
    class RenderPass;
    class CommandBuffer;
    class Window;
    class FrameBuffer;
    class DescriptorPool;
}

namespace CometEditor {
    class ImGuiContext {
    public:
        ImGuiContext(const Comet::Window* window, Comet::RenderContext* render_context);
        ~ImGuiContext();

        ImGuiContext(const ImGuiContext&) = delete;
        ImGuiContext& operator=(const ImGuiContext&) = delete;

        void begin_frame();
        void end_frame();
        void render(Comet::CommandBuffer& command_buffer);

        // Swapchain 重建时调用
        void recreate_swapchain();

        using UICallback = std::function<void()>;
        void set_ui_callback(UICallback callback) { m_ui_callback = std::move(callback); }

    private:
        void init_vulkan();
        void create_render_pass();
        void create_framebuffers();
        void cleanup();

        const Comet::Window* m_window;
        Comet::RenderContext* m_render_context;
        std::unique_ptr<Comet::RenderPass> m_render_pass;
        std::vector<std::shared_ptr<Comet::FrameBuffer>> m_framebuffers;
        std::unique_ptr<Comet::DescriptorPool> m_descriptor_pool;
        UICallback m_ui_callback;
        bool m_initialized = false;
        bool m_is_recreating = false;  // 标记是否正在重建 swapchain
    };
}

