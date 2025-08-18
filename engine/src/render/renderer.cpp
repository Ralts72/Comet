#include "renderer.h"
#include "core/logger/logger.h"
#include "graphics/image.h"
#include "triangle_vert.h"
#include "triangle_frag.h"
#include "cube_vert.h"
#include "cube_frag.h"
#include "graphics/vertex_description.h"
#include "core/math_utils.h"
#include "common/constant.h"
#include "common/geometry_utils.h"

namespace Comet {
    static VkSettings s_vk_settings = {
        .surface_format = vk::Format::eB8G8R8A8Unorm,
        .color_space = vk::ColorSpaceKHR::eSrgbNonlinear,
        .depth_format = vk::Format::eD32Sfloat,
        .present_mode = vk::PresentModeKHR::eImmediate,
        .swapchain_image_count = 3
    };

    static PipelineConfig s_pipeline_config;
    static int buffer_count = static_cast<int>(s_vk_settings.swapchain_image_count);
    static int current_buffer = 0;
    static PushConstant s_push_constant = {
        .matrix = Mat4{1.0f}
    };
    static auto [vertices, indices] = GeometryUtils::create_cube(-0.3f, 0.3f, -0.3f, 0.3f, -0.3f, 0.3f);

    Renderer::Renderer(const Window& window) {
        LOG_INFO("init graphics system");
        m_context = std::make_unique<Context>(window);

        LOG_INFO("create device");
        m_device = std::make_shared<Device>(m_context.get(), 1, 1, s_vk_settings);
        m_graphics_queue = m_device->get_graphics_queue(0);
        m_present_queue = m_device->get_present_queue(0);

        LOG_INFO("create swapchain");
        m_swapchain = std::make_shared<Swapchain>(m_context.get(), m_device.get());

        LOG_INFO("create renderpass");
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

        LOG_INFO("create framebuffer");
        ImageInfo color_image_info = {
            .format = s_vk_settings.surface_format,
            .extent = {m_swapchain->get_width(), m_swapchain->get_height(), 1},
            .usage = vk::ImageUsageFlagBits::eColorAttachment
        };
        ImageInfo depth_image_info = {
            .format = s_vk_settings.depth_format,
            .extent = {m_swapchain->get_width(), m_swapchain->get_height(), 1},
            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment
        };
        for(const auto& image: m_swapchain->get_images()) {
            std::vector<std::shared_ptr<Image>> images = {std::make_shared<Image>(m_device.get(), image.get_image(),
                color_image_info), {std::make_shared<Image>(m_device.get(), depth_image_info)}};
            auto frame_buffer = std::make_shared<FrameBuffer>(m_device.get(), m_render_pass.get(),
                images, m_swapchain->get_width(), m_swapchain->get_height());
            m_frame_buffers.push_back(frame_buffer);
        }

        LOG_INFO("create shader manager");
        m_shader_manager = std::make_unique<ShaderManager>(m_device.get());
        ShaderLayout layout = {};
        layout.push_constants.emplace_back(vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstant));
        // auto vert_shader = m_shader_manager->load_shader("triangle_vert", TRIANGLE_VERT, layout);
        // auto frag_shader = m_shader_manager->load_shader("triangle_frag", TRIANGLE_FRAG, layout);
        auto vert_shader = m_shader_manager->load_shader("cube_vert", CUBE_VERT, layout);
        auto frag_shader = m_shader_manager->load_shader("cube_frag", CUBE_FRAG, layout);
        LOG_INFO("create pipeline");
        auto pipeline_layout = std::make_shared<PipelineLayout>(m_device.get(), layout);
        VertexInputDescription vertex_input_description;
        vertex_input_description.add_binding(0, sizeof(Vertex), vk::VertexInputRate::eVertex);
        vertex_input_description.add_attribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position));
        vertex_input_description.add_attribute(1, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texcoord));
        vertex_input_description.add_attribute(2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal));

        s_pipeline_config.set_vertex_input_state(vertex_input_description.get_bindings(), vertex_input_description.get_attributes());
        s_pipeline_config.set_input_assembly_state(vk::PrimitiveTopology::eTriangleList);
        s_pipeline_config.set_dynamic_state({vk::DynamicState::eViewport, vk::DynamicState::eScissor});
        s_pipeline_config.enable_depth_test();

        m_pipeline = std::make_shared<Pipeline>("triangle_pipeline", m_device.get(), m_render_pass.get(),
            pipeline_layout, vert_shader, frag_shader, s_pipeline_config);

        LOG_INFO("create command pool");
        // m_command_pool = std::make_shared<CommandPool>(m_device.get(), m_context->get_graphics_queue_family().queue_family_index.value());
        m_command_buffers = m_device->get_default_command_pool()->allocate_command_buffers(m_swapchain->get_images().size());

        LOG_INFO("create fence and semaphore");
        for(int i = 0; i < buffer_count; ++i) {
            m_frame_fences.emplace_back(m_device.get());
            m_image_semaphores.emplace_back(m_device.get());
            m_submit_semaphores.emplace_back(m_device.get());
        }

        m_clear_values = {
            vk::ClearColorValue(std::array<float, 4>{0.2f, 0.3f, 0.3f, 1.0f}),
            vk::ClearDepthStencilValue(1.0f, 0)
        };

        m_vertex_buffer = std::make_shared<Buffer>(m_device.get(), vk::BufferUsageFlagBits::eVertexBuffer,
            vertices.size() * sizeof(Vertex), vertices.data());
        m_index_buffer = std::make_shared<Buffer>(m_device.get(), vk::BufferUsageFlagBits::eIndexBuffer,
            sizeof(uint32_t) * indices.size(), indices.data());
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        m_device->wait_idle();

        m_index_buffer.reset();
        m_vertex_buffer.reset();
        m_frame_fences.clear();
        m_submit_semaphores.clear();
        m_image_semaphores.clear();
        m_clear_values.clear();
        m_command_buffers.clear();
        m_pipeline.reset();
        m_shader_manager.reset();
        m_frame_buffers.clear();
        m_render_pass.reset();
        m_swapchain.reset();
        m_present_queue.reset();
        m_graphics_queue.reset();
        m_device.reset();
        m_context.reset();
    }

    void Renderer::on_render(float delta_time) {
        // Update MVP matrix
        Mat4 model{1.0f};
        Mat4 view = look_at(Vec3(2.0f, 2.0f, 2.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
        Mat4 projection = perspective(45.0f, static_cast<float>(m_swapchain->get_width()) / static_cast<float>(m_swapchain->get_height()), 0.1f, 100.0f);
        s_push_constant.matrix = projection * view * model;

        // 1. wait for fence
        const auto& fence = m_frame_fences[current_buffer];
        m_device->wait_for_fences(std::span(&fence, 1));
        m_device->reset_fences(std::span(&fence, 1));

        // 2. acquire swapchain image
        const uint32_t image_index = m_swapchain->acquire_next_image(m_image_semaphores[current_buffer]);

        auto& command_buffer = m_command_buffers[image_index];
        const auto& wait_sem = m_image_semaphores[current_buffer];
        const auto& signal_sem = m_submit_semaphores[current_buffer];
        // 3. begin command buffer
        command_buffer.begin();

        // 4. begin render pass, bind frame buffer
        command_buffer.begin_render_pass(*m_render_pass, *m_frame_buffers[image_index], m_clear_values);

        // 5. bind pipeline
        command_buffer.bind_pipeline(*m_pipeline);

        // 6. set dynamic states (viewport and scissor)
        const auto viewport = get_viewport(static_cast<float>(m_swapchain->get_width()),
            static_cast<float>(m_swapchain->get_height()));
        command_buffer.set_viewport(viewport);

        const auto scissor = get_scissor(static_cast<float>(m_swapchain->get_width()),
            static_cast<float>(m_swapchain->get_height()));
        command_buffer.set_scissor(scissor);

        const Buffer* bufArray[] = { m_vertex_buffer.get() };
        constexpr vk::DeviceSize offsets[] = { 0 };
        command_buffer.bind_vertex_buffer(bufArray, offsets, 0);
        command_buffer.bind_index_buffer(*m_index_buffer, 0, vk::IndexType::eUint32);
        command_buffer.push_constants(*m_pipeline->get_layout(), vk::ShaderStageFlagBits::eVertex, 0,
            &s_push_constant, sizeof(PushConstant));
        // 7. draw
        // command_buffer.draw(3, 1, 0, 0);
        command_buffer.draw_indexed(indices.size(), 1, 0, 0, 0);
        // 8. end render pass
        command_buffer.end_render_pass();

        // 9. end command buffer
        command_buffer.end();

        // 10. submit with fence
        m_graphics_queue->submit(std::span(&command_buffer, 1),
            std::span(&wait_sem, 1), std::span(&signal_sem, 1), &fence);

        // 11. present
        m_swapchain->present(image_index, std::span(&signal_sem, 1));

        current_buffer = (current_buffer + 1) % buffer_count;
    }
}
