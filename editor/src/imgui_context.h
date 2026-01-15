#pragma once
#include "graphics/vk_common.h"
#include <functional>

namespace Comet {
    class Device;
    class Context;
    class RenderPass;
    class CommandBuffer;
    class Window;
}

namespace CometEditor {
    class ImGuiContext {
    public:
        ImGuiContext(const Comet::Window* window, Comet::Context* context, Comet::Device* device, Comet::RenderPass* render_pass);
        ~ImGuiContext();

        ImGuiContext(const ImGuiContext&) = delete;
        ImGuiContext& operator=(const ImGuiContext&) = delete;

        void begin_frame();
        void end_frame();
        void render(Comet::CommandBuffer& command_buffer);

        using UICallback = std::function<void()>;
        void set_ui_callback(UICallback callback) { m_ui_callback = std::move(callback); }

    private:
        void init_vulkan(Comet::Context* context, Comet::Device* device, Comet::RenderPass* render_pass);
        void cleanup();

        vk::DescriptorPool m_descriptor_pool;
        Comet::Device* m_device;
        UICallback m_ui_callback;
        bool m_initialized = false;
    };
}

