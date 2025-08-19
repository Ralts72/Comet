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
    struct FrameResources {
        Fence fence;
        Semaphore image_semaphore;
        Semaphore submit_semaphore;

        explicit FrameResources(Device* device)
            : fence(device), image_semaphore(device), submit_semaphore(device) {}
    };

    class Renderer {
    public:
        explicit Renderer(const Window& window);

        ~Renderer();

        void on_render(float delta_time);

    private:
        std::unique_ptr<Context> m_context;
        std::shared_ptr<Device> m_device;
        std::shared_ptr<Swapchain> m_swapchain;
        std::shared_ptr<RenderPass> m_render_pass;

        std::unique_ptr<ShaderManager> m_shader_manager;
        mutable std::shared_ptr<Pipeline> m_pipeline;
        mutable std::vector<std::shared_ptr<FrameBuffer>> m_frame_buffers;
        mutable bool m_frame_buffers_dirty = false;

        mutable std::vector<FrameResources> m_frame_resources;

        std::shared_ptr<Buffer> m_vertex_buffer;
        std::shared_ptr<Buffer> m_index_buffer;

        std::vector<CommandBuffer> m_command_buffers;
    };
}


