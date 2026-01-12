#include "renderer.h"
#include "common/logger.h"
#include "graphics/image.h"
#include "graphics/vertex_description.h"
#include "common/geometry_utils.h"
#include "common/profiler.h"
#include "graphics/queue.h"
#include "graphics/image_view.h"

namespace Comet {
    static VkSettings s_vk_settings = {
        .surface_format = Format::B8G8R8A8_UNORM,
        .color_space = ImageColorSpace::SrgbNonlinearKHR,
        .depth_format = Format::D32_SFLOAT,
        .present_mode = vk::PresentModeKHR::eImmediate,
        .swapchain_image_count = 3,
        .msaa_samples = SampleCount::Count4
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
        m_render_target->set_clear_value(ClearValue(Math::Vec4(0.2f, 0.4f, 0.1f, 1.0f)));
        LOG_INFO("create command buffers");
        m_command_buffers = m_device->get_default_command_pool().allocate_command_buffers(m_swapchain->get_images().size());

        LOG_INFO("create fence and semaphore");
        for(uint32_t i = 0; i < s_vk_settings.swapchain_image_count; ++i) {
            m_frame_resources.emplace_back(m_device.get());
        }

        LOG_INFO("create shader manager");
        m_shader_manager = std::make_unique<ShaderManager>(m_device.get());
        ShaderLayout layout = {};
        // layout.push_constants.emplace_back(vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstant));
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        bindings.add_binding(1, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        bindings.add_binding(2, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        bindings.add_binding(3, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        m_descriptor_set_layout = std::make_shared<DescriptorSetLayout>(m_device.get(), bindings);
        layout.descriptor_set_layouts.push_back(m_descriptor_set_layout);

        auto vert_shader = m_shader_manager->load_shader("cube_texture_vert", CUBE_TEXTURE_VERT, layout);
        auto frag_shader = m_shader_manager->load_shader("cube_texture_frag", CUBE_TEXTURE_FRAG, layout);
        LOG_INFO("create pipeline");
        auto pipeline_layout = std::make_shared<PipelineLayout>(m_device.get(), layout);
        VertexInputDescription vertex_input_description;
        vertex_input_description.add_binding(0, sizeof(Math::Vertex), VertexInputRate::Vertex);
        vertex_input_description.add_attribute(0, 0, Format::R32G32B32_SFLOAT, offsetof(Math::Vertex, position));
        vertex_input_description.add_attribute(1, 0, Format::R32G32_SFLOAT, offsetof(Math::Vertex, texcoord));
        vertex_input_description.add_attribute(2, 0, Format::R32G32B32_SFLOAT, offsetof(Math::Vertex, normal));

        PipelineConfig pipeline_config = {};
        pipeline_config.set_vertex_input_state(vertex_input_description);
        pipeline_config.set_input_assembly_state(Topology::TriangleList);
        pipeline_config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        pipeline_config.enable_depth_test();
        pipeline_config.set_multisample_state(s_vk_settings.msaa_samples, false, 0.2f);

        m_pipeline = std::make_shared<Pipeline>("cube_pipeline", m_device.get(), m_render_pass.get(),
            pipeline_layout, vert_shader, frag_shader, pipeline_config);

        LOG_INFO("create descriptor pool and descriptor sets");
        DescriptorPoolSizes descriptor_pool_sizes;
        descriptor_pool_sizes.add_pool_size(DescriptorType::UniformBuffer, 2);
        descriptor_pool_sizes.add_pool_size(DescriptorType::CombinedImageSampler, 2);
        m_descriptor_pool = std::make_shared<DescriptorPool>(m_device.get(), 1, descriptor_pool_sizes);
        m_descriptor_sets = m_descriptor_pool->allocate_descriptor_set(*m_descriptor_set_layout, 1);

        m_view_project_uniform_buffer = Buffer::create_cpu_buffer(m_device.get(), Flags<BufferUsage>(BufferUsage::Uniform),
            sizeof(ViewProjectMatrix), nullptr);

        m_model_uniform_buffer = Buffer::create_cpu_buffer(m_device.get(), Flags<BufferUsage>(BufferUsage::Uniform),
            sizeof(ModelMatrix), nullptr);
        LOG_INFO("create sampler manager and textures");
        m_sampler_manager = std::make_shared<SamplerManager>(m_device.get());
        auto sampler = m_sampler_manager->get_linear_repeat();
        std::string image_path = std::string(PROJECT_ROOT_DIR) + "/engine/assets/textures/";
        m_texture1 = std::make_shared<Texture>(m_device.get(), image_path + "awesomeface.png");
        m_texture2 = std::make_shared<Texture>(m_device.get(), image_path + "R-C.jpeg");

        auto [cube_vertices, cube_indices] = GeometryUtils::create_cube(-0.3f, 0.3f, -0.3f, 0.3f, -0.3f, 0.3f);
        m_cube_mesh = std::make_shared<Mesh>(m_device.get(), cube_vertices, cube_indices);
    }

    void Renderer::on_update(const float delta_time) {
        PROFILE_SCOPE("render update");
        total_time += delta_time;
        // Update MVP matrix
        // auto model = Math::Mat4(1.0f);
        // model = Math::rotate(model, Math::radians(-17.0f), Math::Vec3(1.0f, 0.0f, 0.0f));
        // model = Math::rotate(model, Math::radians(total_time * 100.0f), Math::Vec3(0.0f, 1.0f, 0.0f));
        // auto projection = Math::perspective(Math::radians(65.0f),
        //     static_cast<float>(m_swapchain->get_width()) / static_cast<float>(m_swapchain->get_height()), 0.1f, 100.0f);
        // auto view = Math::look_at(Math::Vec3(30.0f, 0.0f, 30.0f),
        //     Math::Vec3(0.0f, 0.0f, 0.0f),
        //     Math::Vec3(0.0f, 1.0f, 0.0f));
        // m_push_constant.matrix = projection * view * model;

        m_model_matrix.model = Math::rotate(Math::Mat4(1.0f), Math::radians(-17.0f), Math::Vec3(1.0f, 0.0f, 0.0f));
        m_model_matrix.model = Math::rotate(m_model_matrix.model, Math::radians(total_time * 100.0f), Math::Vec3(0.0f, 1.0f, 0.0f));

        m_view_project_matrix.view = Math::look_at(Math::Vec3(0.0f, 0.0f, 30.5f),
            Math::Vec3(0.0f, 0.0f, -10.0f),
            Math::Vec3(0.0f, 1.0f, 0.0f));
        m_view_project_matrix.projection = Math::perspective(Math::radians(45.0f),
            static_cast<float>(m_swapchain->get_width()) / static_cast<float>(m_swapchain->get_height()), 0.1f, 100.0f);
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
        const auto viewport = Graphics::get_viewport(static_cast<float>(m_swapchain->get_width()),
            static_cast<float>(m_swapchain->get_height()));
        command_buffer.set_viewport(viewport);

        const auto scissor = Graphics::get_scissor(static_cast<float>(m_swapchain->get_width()),
            static_cast<float>(m_swapchain->get_height()));
        command_buffer.set_scissor(scissor);

        static_pointer_cast<CPUBuffer>(m_view_project_uniform_buffer)->write(&m_view_project_matrix);
        static_pointer_cast<CPUBuffer>(m_model_uniform_buffer)->write(&m_model_matrix);
        updateDescriptorSets();

        std::vector<vk::DescriptorSet> descriptor_sets;
        descriptor_sets.reserve(m_descriptor_sets.size());
        for(auto ds: m_descriptor_sets) {
            descriptor_sets.push_back(ds.get());
        }
        command_buffer.get().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline->get_layout()->get(),
            0, 1, descriptor_sets.data(), 0, nullptr);
        // 7. draw
        // command_buffer.push_constants(*m_pipeline->get_layout(), Flags<ShaderStage>(ShaderStage::Vertex), 0,
        // &m_push_constant, sizeof(PushConstant));
        m_cube_mesh->draw(command_buffer);

        // 8. end render pass
        m_render_target->end_render_target(command_buffer);

        // 9. end command buffer
        command_buffer.end();

        // 10. submit with fence
        auto& graphics_queue = m_device->get_graphics_queue(0);
        graphics_queue.submit(std::span(&command_buffer, 1),
            std::span(&wait_sem, 1), std::span(&signal_sem, 1), &fence);

        // 11. present
        auto& present_queue = m_device->get_present_queue(0);
        const auto result = present_queue.present(*m_swapchain, std::span(&signal_sem, 1), image_index);
        if(result == vk::Result::eSuboptimalKHR) {
            recreate_swapchain();
        }

        m_current_buffer = (m_current_buffer + 1) % s_vk_settings.swapchain_image_count;
    }

    void Renderer::recreate_swapchain() {
        PROFILE_SCOPE("recreate_swapchain");
        m_device->wait_idle();
        const auto original_size = Math::Vec2u(m_swapchain->get_width(), m_swapchain->get_height());
        const bool flag = m_swapchain->recreate();
        const auto size = Math::Vec2u(m_swapchain->get_width(), m_swapchain->get_height());
        if(flag && original_size != size) {
            m_render_target->resize(size.x, size.y);
        }
    }

    void Renderer::updateDescriptorSets() {
        vk::DescriptorBufferInfo buffer_info1{};
        buffer_info1.buffer = m_view_project_uniform_buffer->get();
        buffer_info1.offset = 0;
        buffer_info1.range = sizeof(ViewProjectMatrix);
        vk::DescriptorBufferInfo buffer_info2{};
        buffer_info2.buffer = m_model_uniform_buffer->get();
        buffer_info2.offset = 0;
        buffer_info2.range = sizeof(ModelMatrix);
        vk::DescriptorImageInfo image_info1{};
        image_info1.sampler = m_sampler_manager->get_linear_repeat()->get();
        image_info1.imageView = m_texture1->get_image_view()->get();
        image_info1.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        vk::DescriptorImageInfo image_info2{};
        image_info2.sampler = m_sampler_manager->get_linear_repeat()->get();
        image_info2.imageView = m_texture2->get_image_view()->get();
        image_info2.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        DescriptorSet set = m_descriptor_sets[0];
        std::vector<vk::WriteDescriptorSet> write_sets;
        vk::WriteDescriptorSet set1{};
        set1.dstSet = set.get();
        set1.dstBinding = 0;
        set1.dstArrayElement = 0;
        set1.descriptorType = vk::DescriptorType::eUniformBuffer;
        set1.descriptorCount = 1;
        set1.pBufferInfo = &buffer_info1;
        write_sets.emplace_back(set1);
        vk::WriteDescriptorSet set2{};
        set2.dstSet = set.get();
        set2.dstBinding = 1;
        set2.dstArrayElement = 0;
        set2.descriptorType = vk::DescriptorType::eUniformBuffer;
        set2.descriptorCount = 1;
        set2.pBufferInfo = &buffer_info2;
        write_sets.emplace_back(set2);
        vk::WriteDescriptorSet set3{};
        set3.dstSet = set.get();
        set3.dstBinding = 2;
        set3.dstArrayElement = 0;
        set3.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        set3.descriptorCount = 1;
        set3.pImageInfo = &image_info1;
        write_sets.emplace_back(set3);
        vk::WriteDescriptorSet set4{};
        set4.dstSet = set.get();
        set4.dstBinding = 3;
        set4.dstArrayElement = 0;
        set4.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        set4.descriptorCount = 1;
        set4.pImageInfo = &image_info2;
        write_sets.emplace_back(set4);
        m_device->get().updateDescriptorSets(write_sets, {});
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        m_device->wait_idle();

        m_view_project_uniform_buffer.reset();
        m_model_uniform_buffer.reset();
        m_texture1.reset();
        m_texture2.reset();
        m_sampler_manager.reset();
        m_descriptor_pool.reset();
        m_descriptor_set_layout.reset();

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
