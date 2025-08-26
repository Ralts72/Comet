#include "renderer.h"
#include "core/logger/logger.h"
#include "graphics/image.h"
#include "graphics/vertex_description.h"
#include "common/geometry_utils.h"
#include "common/profiler.h"
#include "graphics/queue.h"

namespace Comet {
    static VkSettings s_vk_settings = {
        .surface_format = vk::Format::eB8G8R8A8Unorm,
        .color_space = vk::ColorSpaceKHR::eSrgbNonlinear,
        .depth_format = vk::Format::eD32Sfloat,
        .present_mode = vk::PresentModeKHR::eImmediate,
        .swapchain_image_count = 3,
        .msaa_samples = vk::SampleCountFlagBits::e4
    };

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
        attachments.emplace_back(Attachment::get_color_attachment(s_vk_settings.surface_format,
            s_vk_settings.msaa_samples));
        attachments.emplace_back(Attachment::get_depth_attachment(s_vk_settings.depth_format,
            s_vk_settings.msaa_samples));

        std::vector<RenderSubPass> render_sub_passes;
        RenderSubPass render_sub_pass_0 = {
            {},
            {SubpassColorAttachment(0)},
            {SubpassDepthStencilAttachment(1)},
            s_vk_settings.msaa_samples
        };
        render_sub_passes.emplace_back(render_sub_pass_0);

        m_render_pass = std::make_shared<RenderPass>(m_device.get(), attachments, render_sub_passes);

        LOG_INFO("create render target");
        m_render_target = RenderTarget::create_swapchain_target(m_device.get(), m_render_pass.get(), m_swapchain.get());
        m_render_target->set_color_clear_value(Math::Vec4{0.2f, 0.8f, 0.1f, 1.0f});
        LOG_INFO("create command buffers");
        m_command_buffers = m_device->get_default_command_pool()->allocate_command_buffers(m_swapchain->get_images().size());

        LOG_INFO("create fence and semaphore");
        for(uint32_t i = 0; i < s_vk_settings.swapchain_image_count; ++i) {
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
        pipeline_config.set_multisample_state(s_vk_settings.msaa_samples, VK_FALSE, 0.2f);

        m_pipeline = std::make_shared<Pipeline>("cube_pipeline", m_device.get(), m_render_pass.get(),
            pipeline_layout, vert_shader, frag_shader, pipeline_config);

        auto [cube_vertices, cube_indices] = GeometryUtils::create_cube(-0.3f, 0.3f, -0.3f, 0.3f, -0.3f, 0.3f);
        m_cube_mesh = std::make_shared<Mesh>(m_device.get(), cube_vertices, cube_indices);
    }

    void Renderer::on_update(const float delta_time) {
        PROFILE_SCOPE("render update");
        total_time += delta_time;
        // Update MVP matrix
        auto model = Math::Mat4(1.0f);
        model = Math::rotate(model, Math::radians(-17.0f), Math::Vec3(1.0f, 0.0f, 0.0f));
        model = Math::rotate(model, Math::radians(total_time * 100.0f), Math::Vec3(0.0f, 1.0f, 0.0f));
        auto projection = Math::perspective(Math::radians(65.0f),
            static_cast<float>(m_swapchain->get_width()) / static_cast<float>(m_swapchain->get_height()), 0.1f, 100.0f);
        auto view = Math::look_at(Math::Vec3(30.0f, 0.0f, 30.0f),
            Math::Vec3(0.0f, 0.0f, 0.0f),
            Math::Vec3(0.0f, 1.0f, 0.0f));
        m_push_constant.matrix = projection * view * model;
    }

    void Renderer::on_render() {
        PROFILE_SCOPE("render frame");
        // 1. wait for fence
        const auto& fence = m_frame_resources[m_current_buffer].fence;
        m_device->wait_for_fences(std::span(&fence, 1));
        m_device->reset_fences(std::span(&fence, 1));

        // 2. acquire swapchain image
        const auto& wait_sem = m_frame_resources[m_current_buffer].image_semaphore;
        auto [image_index, acquire_result] = m_swapchain->acquire_next_image(wait_sem);
        if(acquire_result == vk::Result::eErrorOutOfDateKHR) {
            recreate_swapchain();
            std::tie(image_index, acquire_result) = m_swapchain->acquire_next_image(wait_sem);
            if(acquire_result != vk::Result::eSuccess && acquire_result != vk::Result::eSuboptimalKHR) {
                LOG_FATAL("can't acquire swapchain image");
            }
        }

        auto& command_buffer = m_command_buffers[image_index];
        const auto& signal_sem = m_frame_resources[m_current_buffer].submit_semaphore;
        // 3. begin command buffer
        command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

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

        // 7. draw
        command_buffer.push_constants(*m_pipeline->get_layout(), vk::ShaderStageFlagBits::eVertex, 0,
            &m_push_constant, sizeof(PushConstant));
        m_cube_mesh->draw(command_buffer);

        // 8. end render pass
        m_render_target->end_render_target(command_buffer);

        // 9. end command buffer
        command_buffer.end();

        // 10. submit with fence
        auto graphics_queue = m_device->get_graphics_queue(0);
        graphics_queue->submit(std::span(&command_buffer, 1),
            std::span(&wait_sem, 1), std::span(&signal_sem, 1), &fence);

        // 11. present
        auto present_queue = m_device->get_present_queue(0);
        const auto result = present_queue->present(*m_swapchain, std::span(&signal_sem, 1), image_index);
        if(result == vk::Result::eSuboptimalKHR) {
            recreate_swapchain();
        }

        m_current_buffer = (m_current_buffer + 1) % s_vk_settings.swapchain_image_count;
    }

    void Renderer::recreate_swapchain() {
        PROFILE_SCOPE("recreate_swapchain");
        m_device->wait_idle();
        const auto original_size = Math::Vec2(m_swapchain->get_width(), m_swapchain->get_height());
        const bool flag = m_swapchain->recreate();
        const auto size = Math::Vec2(m_swapchain->get_width(), m_swapchain->get_height());
        if(flag && original_size != size) {
            m_render_target->resize(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));
        }
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        m_device->wait_idle();
        m_cube_mesh.reset();
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
