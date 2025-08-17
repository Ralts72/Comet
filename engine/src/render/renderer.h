#pragma once
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/swapchain.h"
#include "graphics/render_pass.h"
#include "graphics/frame_buffer.h"
#include "graphics/shader.h"
#include "graphics/pipeline.h"
#include "graphics/command_buffer.h"

namespace Comet {
    class Renderer {
    public:
        explicit Renderer(const Window& window);
        ~Renderer();

        void on_render(float time);
    private:
        std::unique_ptr<Context> m_context;
        std::shared_ptr<Device> m_device;
        std::shared_ptr<Swapchain> m_swapchain;
        std::shared_ptr<RenderPass> m_render_pass;
        std::vector<std::shared_ptr<FrameBuffer>> m_frame_buffers;
        std::unique_ptr<ShaderManager> m_shader_manager;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<CommandPool> m_command_pool;
    };
}


