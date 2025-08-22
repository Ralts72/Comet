#include "renderer.h"
#include "core/logger/logger.h"
#include "graphics/image.h"
#include "graphics/vertex_description.h"
#include "common/shader_resources.h"
#include "common/geometry_utils.h"
#include "common/profiler.h"
#include "graphics/queue.h"

namespace Comet {
    static VkSettings s_vk_settings = {
        .surface_format = vk::Format::eB8G8R8A8Unorm,
        .color_space = vk::ColorSpaceKHR::eSrgbNonlinear,
        .depth_format = vk::Format::eD32Sfloat,
        .present_mode = vk::PresentModeKHR::eImmediate,
        .swapchain_image_count = 3
    };

    static int buffer_count = static_cast<int>(s_vk_settings.swapchain_image_count);

    static int current_buffer = 0;

    static PushConstant s_push_constant = {
        .matrix = Math::Mat4{1.0f}
    };

    static auto [s_cube_vertices, s_cube_indices] = GeometryUtils::create_cube(-0.3f, 0.3f, -0.3f, 0.3f, -0.3f, 0.3f);

    static float total_time = 0.0f;

    Renderer::Renderer(const Window& window) {
        PROFILE_SCOPE("Renderer::Constructor");
        LOG_INFO("init graphics system");
        m_context = std::make_unique<Context>(window);

        LOG_INFO("create device");
        m_device = std::make_shared<Device>(m_context.get(), 1, 1, s_vk_settings);

        LOG_INFO("create swapchain");
        m_swapchain = std::make_shared<Swapchain>(m_context.get(), m_device.get());

        LOG_INFO("create render pass");
        std::vector<Attachment> attachments;
        vk::AttachmentDescription color_attachment = {};
        color_attachment.format = s_vk_settings.surface_format;
        color_attachment.samples = vk::SampleCountFlagBits::e1;
        color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
        color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        color_attachment.initialLayout = vk::ImageLayout::eUndefined;
        color_attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
        attachments.push_back({color_attachment, vk::ImageUsageFlagBits::eColorAttachment});
        vk::AttachmentDescription depth_attachment = {};
        depth_attachment.format = s_vk_settings.depth_format;
        depth_attachment.samples = vk::SampleCountFlagBits::e1;
        depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        depth_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
        depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        attachments.push_back({depth_attachment, vk::ImageUsageFlagBits::eDepthStencilAttachment});

        std::vector<RenderSubPass> render_sub_passes;
        RenderSubPass render_sub_pass_0 = {
            {},
            {SubpassColorAttachment(0)},
            {SubpassDepthStencilAttachment(1)},
            vk::SampleCountFlagBits::e1
        };
        render_sub_passes.emplace_back(render_sub_pass_0);

        m_render_pass = std::make_shared<RenderPass>(m_device.get(), attachments, render_sub_passes);

        LOG_INFO("create render target");
        m_render_target = RenderTarget::create_swapchain_target(m_device.get(), m_render_pass.get(), m_swapchain.get());
        m_render_target->set_color_clear_value(Math::Vec4{0.2f, 0.8f, 0.1f, 1.0f});
        LOG_INFO("create command buffers");
        m_command_buffers = m_device->get_default_command_pool()->allocate_command_buffers(m_swapchain->get_images().size());

        LOG_INFO("create fence and semaphore");
        for(int i = 0; i < buffer_count; ++i) {
            m_frame_resources.emplace_back(m_device.get());
        }

        LOG_INFO("create shader manager");
        m_shader_manager = std::make_unique<ShaderManager>(m_device.get());
        ShaderLayout layout = {};
        layout.push_constants.emplace_back(vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstant));
        auto vert_shader = m_shader_manager->load_shader("cube_vert", CUBE_VERT, layout);
        auto frag_shader = m_shader_manager->load_shader("cube_frag", CUBE_FRAG, layout);
        LOG_INFO("create pipeline");
        auto pipeline_layout = std::make_shared<PipelineLayout>(m_device.get(), layout);
        VertexInputDescription vertex_input_description;
        vertex_input_description.add_binding(0, sizeof(Math::Vertex), vk::VertexInputRate::eVertex);
        vertex_input_description.add_attribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Math::Vertex, position));
        vertex_input_description.add_attribute(1, 0, vk::Format::eR32G32Sfloat, offsetof(Math::Vertex, texcoord));
        vertex_input_description.add_attribute(2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Math::Vertex, normal));

        PipelineConfig pipeline_config = {};
        pipeline_config.set_vertex_input_state(vertex_input_description.get_bindings(), vertex_input_description.get_attributes());
        pipeline_config.set_input_assembly_state(vk::PrimitiveTopology::eTriangleList);
        pipeline_config.set_dynamic_state({vk::DynamicState::eViewport, vk::DynamicState::eScissor});
        pipeline_config.enable_depth_test();

        m_pipeline = std::make_shared<Pipeline>("cube_pipeline", m_device.get(), m_render_pass.get(),
            pipeline_layout, vert_shader, frag_shader, pipeline_config);

        m_vertex_buffer = std::make_shared<Buffer>(m_device.get(), vk::BufferUsageFlagBits::eVertexBuffer,
            s_cube_vertices.size() * sizeof(Math::Vertex), s_cube_vertices.data());
        m_index_buffer = std::make_shared<Buffer>(m_device.get(), vk::BufferUsageFlagBits::eIndexBuffer,
            sizeof(uint32_t) * s_cube_indices.size(), s_cube_indices.data());
    }

    void Renderer::on_render(const float delta_time) {
        PROFILE_SCOPE("render");
        total_time += delta_time;
        // Update MVP matrix
        auto model = Math::Mat4(1.0f);
        model = rotate(model, Math::radians(-17.0f), Math::Vec3(1.0f, 0.0f, 0.0f));
        model = rotate(model, Math::radians(total_time * 100.0f), Math::Vec3(0.0f, 1.0f, 0.0f));
        s_push_constant.matrix = Math::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f) * model;
        // 1. wait for fence
        const auto& fence = m_frame_resources[current_buffer].fence;
        m_device->wait_for_fences(std::span(&fence, 1));
        m_device->reset_fences(std::span(&fence, 1));

        // 2. acquire swapchain image
        const auto& wait_sem = m_frame_resources[current_buffer].image_semaphore;
        const uint32_t image_index = m_swapchain->acquire_next_image(wait_sem);

        auto& command_buffer = m_command_buffers[image_index];
        const auto& signal_sem = m_frame_resources[current_buffer].submit_semaphore;
        // 3. begin command buffer
        command_buffer.begin();

        // 4. begin render target
        m_render_target->begin_render_target(command_buffer);

        // 5. bind pipeline
        command_buffer.bind_pipeline(*m_pipeline);

        // 6. set dynamic states (viewport and scissor)
        const auto viewport = get_viewport(static_cast<float>(m_swapchain->get_width()),
            static_cast<float>(m_swapchain->get_height()));
        command_buffer.set_viewport(viewport);

        const auto scissor = get_scissor(static_cast<float>(m_swapchain->get_width()),
            static_cast<float>(m_swapchain->get_height()));
        command_buffer.set_scissor(scissor);

        const Buffer* buffer_array[] = {m_vertex_buffer.get()};
        constexpr vk::DeviceSize offsets[] = {0};
        command_buffer.bind_vertex_buffer(buffer_array, offsets, 0);
        command_buffer.bind_index_buffer(*m_index_buffer, 0, vk::IndexType::eUint32);
        command_buffer.push_constants(*m_pipeline->get_layout(), vk::ShaderStageFlagBits::eVertex, 0,
            &s_push_constant, sizeof(PushConstant));

        // 7. draw
        command_buffer.draw_indexed(s_cube_indices.size(), 1, 0, 0, 0);

        // 8. end render pass
        m_render_target->end_render_target(command_buffer);

        // 9. end command buffer
        command_buffer.end();

        // 10. submit with fence
        auto graphics_queue = m_device->get_graphics_queue(0);
        graphics_queue->submit(std::span(&command_buffer, 1),
            std::span(&wait_sem, 1), std::span(&signal_sem, 1), &fence);

        // 11. present
        m_swapchain->present(image_index, std::span(&signal_sem, 1));

        current_buffer = (current_buffer + 1) % buffer_count;
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        m_device->wait_idle();
        m_index_buffer.reset();
        m_vertex_buffer.reset();
        m_frame_resources.clear();
        m_command_buffers.clear();
        m_pipeline.reset();
        m_shader_manager.reset();
        m_render_target.reset();
        m_render_pass.reset();
        m_swapchain.reset();
        m_device.reset();
        m_context.reset();
    }
}
