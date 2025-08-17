#include "renderer.h"
#include "core/logger/logger.h"
#include "graphics/image.h"
#include "triangle_vert.h"
#include "triangle_frag.h"

namespace Comet {
    static VkSettings s_vk_settings = {
        .surface_format = vk::Format::eB8G8R8A8Unorm,
        .color_space = vk::ColorSpaceKHR::eSrgbNonlinear,
        .depth_format = vk::Format::eD32Sfloat,
        .present_mode = vk::PresentModeKHR::eImmediate,
        .swapchain_image_count = 3
    };

    static PipelineConfig s_pipeline_config;
    static int buffer_count = 2;
    static int current_buffer = 0;

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
        m_render_pass = std::make_shared<RenderPass>(m_device.get());

        LOG_INFO("create framebuffer");
        ImageInfo image_info = {
            .format = s_vk_settings.surface_format,
            .extent = {m_swapchain->get_width(), m_swapchain->get_height(), 1},
            .usage = vk::ImageUsageFlagBits::eColorAttachment
        };
        for(const auto& image : m_swapchain->get_images()) {
            std::vector<std::shared_ptr<Image>> images = {std::make_shared<Image>(m_device.get(), image.get_image(), image_info)};
            auto frame_buffer = std::make_shared<FrameBuffer>(m_device.get(), m_render_pass.get(),
                images, m_swapchain->get_width(), m_swapchain->get_height());
            m_frame_buffers.push_back(frame_buffer);
        }

        LOG_INFO("create shader manager");
        m_shader_manager = std::make_unique<ShaderManager>(m_device.get());
        auto vert_shader = m_shader_manager->load_shader("triangle_vert", TRIANGLE_VERT);
        auto frag_shader = m_shader_manager->load_shader("triangle_frag", TRIANGLE_FRAG);

        LOG_INFO("create pipeline");
        ShaderLayout shader_layout = vert_shader->get_layout();
        auto pipeline_layout = std::make_shared<PipelineLayout>(m_device.get(), shader_layout);
        s_pipeline_config.set_input_assembly_state(vk::PrimitiveTopology::eTriangleList);
        s_pipeline_config.set_dynamic_state({vk::DynamicState::eViewport, vk::DynamicState::eScissor});
        s_pipeline_config.enable_depth_test();

        m_pipeline = std::make_shared<Pipeline>("triangle_pipeline", m_device.get(),m_render_pass.get(),
            pipeline_layout, vert_shader, frag_shader, s_pipeline_config);

        LOG_INFO("create command pool");
        m_command_pool = std::make_shared<CommandPool>(m_device.get(), m_context->get_graphics_queue_family().queue_family_index.value());
        m_command_buffers = m_command_pool->allocate_command_buffers(m_swapchain->get_images().size());

        LOG_INFO("create fence and semaphore");
        for(int i = 0; i < buffer_count; ++i) {
            m_frame_fences.emplace_back(m_device.get());
            m_image_semaphores.emplace_back(m_device.get());
            m_submit_semaphores.emplace_back(m_device.get());
        }

        m_clear_values = {vk::ClearColorValue(std::array<float, 4>{0.2f, 0.3f, 0.3f, 1.0f})};
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        m_device->wait_idle();

        m_frame_fences.clear();
        m_submit_semaphores.clear();
        m_image_semaphores.clear();
        m_clear_values.clear();
        m_command_buffers.clear();
        m_command_pool.reset();
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

    void Renderer::on_render(float delta_time) const {
        // 1. wait for fence
        const auto& fence = m_frame_fences[current_buffer];
        m_device->wait_for_fences({&fence});
        m_device->reset_fences({&fence});

        // 2. acquire swapchain image
        const uint32_t image_index = m_swapchain->acquire_next_image(m_image_semaphores[current_buffer], m_frame_fences[current_buffer]);
        
        // 3. begin command buffer
        auto command_buffer = m_command_buffers[image_index];
        command_buffer.begin();
        
        // 4. begin render pass, bind frame buffer
        command_buffer.begin_render_pass(*m_render_pass, *m_frame_buffers[image_index], m_clear_values);
        
        // 5. bind pipeline
        command_buffer.bind_pipeline(*m_pipeline);
        
        // 6. set dynamic states (viewport and scissor)
        const auto viewport = get_viewport(m_swapchain->get_width(), m_swapchain->get_height());
        command_buffer.set_viewport(viewport);
        
        const auto scissor = get_scissor(m_swapchain->get_width(), m_swapchain->get_height());
        command_buffer.set_scissor(scissor);
        
        // 7. draw
        command_buffer.draw(3, 1, 0, 0);
        
        // 8. end render pass
        command_buffer.end_render_pass();
        
        // 9. end command buffer
        command_buffer.end();
        
        // 10. submit with fence
        m_graphics_queue->submit({&m_command_buffers[image_index]},
            {&m_image_semaphores[current_buffer]},
            {&m_submit_semaphores[current_buffer]},
            &m_frame_fences[current_buffer]);

        // 11. present
        m_swapchain->present(image_index, {&m_submit_semaphores[current_buffer]});

        current_buffer = (current_buffer + 1) % buffer_count;
    }
}
