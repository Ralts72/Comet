#pragma once
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/swapchain.h"
#include "graphics/render_pass.h"
#include "graphics/shader.h"
#include "graphics/pipeline.h"
#include "graphics/command_buffer.h"
#include "graphics/fence.h"
#include "graphics/semaphore.h"
#include "render_target.h"
#include "mesh.h"
#include "common/shader_resources.h"

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

        void on_update(float delta_time);
        void on_render();

    private:
        std::unique_ptr<Context> m_context;
        std::shared_ptr<Device> m_device;
        std::shared_ptr<Swapchain> m_swapchain;
        std::shared_ptr<RenderPass> m_render_pass;

        std::unique_ptr<ShaderManager> m_shader_manager;
        mutable std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<RenderTarget> m_render_target;

        mutable std::vector<FrameResources> m_frame_resources;

        std::shared_ptr<Mesh> m_cube_mesh;

        std::vector<CommandBuffer> m_command_buffers;
        uint32_t m_current_buffer = 0;

        PushConstant m_push_constant = {
            .matrix = Math::Mat4{1.0f}
        };
    };
}


