#pragma once
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/swapchain.h"
#include "graphics/render_pass.h"
#include "graphics/frame_buffer.h"
#include "graphics/shader.h"
#include "graphics/pipeline.h"
#include "graphics/command_buffer.h"
#include "graphics/queue.h"
#include "graphics/fence.h"
#include "graphics/semaphore.h"
#include "graphics/buffer.h"

namespace Comet {
    class Renderer {
    public:
        explicit Renderer(const Window& window);

        ~Renderer();

        void on_render(float delta_time);

    private:
        std::unique_ptr<Context> m_context;
        std::shared_ptr<Device> m_device;
        std::shared_ptr<Queue> m_graphics_queue;
        std::shared_ptr<Queue> m_present_queue;
        std::shared_ptr<Swapchain> m_swapchain;
        std::shared_ptr<RenderPass> m_render_pass;
        std::vector<std::shared_ptr<FrameBuffer>> m_frame_buffers;
        std::unique_ptr<ShaderManager> m_shader_manager;
        std::shared_ptr<Pipeline> m_pipeline;
        std::vector<CommandBuffer> m_command_buffers;
        std::vector<vk::ClearValue> m_clear_values;
        std::vector<Fence> m_frame_fences;
        std::vector<Semaphore> m_image_semaphores;
        std::vector<Semaphore> m_submit_semaphores;
        std::shared_ptr<Buffer> m_vertex_buffer;
        std::shared_ptr<Buffer> m_index_buffer;
    };
}


