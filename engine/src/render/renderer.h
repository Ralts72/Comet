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
#include "graphics/descriptor_set.h"
#include "graphics/sampler.h"
#include "render_target.h"
#include "mesh.h"
#include "common/shader_resources.h"
#include "texture.h"

namespace Comet {
    struct FrameSynchronization {
        Fence fence;
        Semaphore image_semaphore;
        Semaphore submit_semaphore;

        explicit FrameSynchronization(Device* device)
            : fence(device), image_semaphore(device), submit_semaphore(device) {}
    };

    class Renderer {
    public:
        explicit Renderer(const Window& window);

        ~Renderer();

        void on_update(float delta_time);

        void on_render();

        void recreate_swapchain();

        // 暂时由renderer实现
        void updateDescriptorSets();
    private:
        std::unique_ptr<Context> m_context;
        std::shared_ptr<Device> m_device;
        std::shared_ptr<Swapchain> m_swapchain;
        std::shared_ptr<RenderPass> m_render_pass;

        std::unique_ptr<ShaderManager> m_shader_manager;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<RenderTarget> m_render_target;

        std::shared_ptr<DescriptorPool> m_descriptor_pool;
        std::shared_ptr<DescriptorSetLayout> m_descriptor_set_layout;
        std::vector<DescriptorSet> m_descriptor_sets;

        std::shared_ptr<Buffer> m_view_project_uniform_buffer;
        std::shared_ptr<Buffer> m_model_uniform_buffer;

        std::shared_ptr<SamplerManager> m_sampler_manager;
        std::shared_ptr<Texture> m_texture1;
        std::shared_ptr<Texture> m_texture2;

        std::vector<FrameSynchronization> m_frame_resources;

        std::shared_ptr<Mesh> m_cube_mesh;

        std::vector<CommandBuffer> m_command_buffers;
        uint32_t m_current_buffer = 0;

        PushConstant m_push_constant = {
            .matrix = Math::Mat4{1.0f}
        };

        ViewProjectMatrix m_view_project_matrix = {
            .view = Math::Mat4{1.0f},
            .projection = Math::Mat4{1.0f}
        };

        ModelMatrix m_model_matrix = {
            .model = Math::Mat4{1.0f}
        };
    };
}
