#include "scene_renderer.h"
#include "common/logger.h"
#include "common/profiler.h"
#include "common/shader_resources.h"
#include "graphics/queue.h"
#include "graphics/image_view.h"
#include "graphics/vk_common.h"
#include "graphics/buffer.h"
#include "graphics/sampler.h"
#include "graphics/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/attachment.h"
#include "graphics/vertex_description.h"
#include "resource_manager.h"

namespace Comet {
    SceneRenderer::SceneRenderer(RenderContext& context,
                                 const Config::Vulkan& vulkan_config,
                                 const Config::Render& render_config)
        : m_context(context), m_vulkan_config(vulkan_config), m_render_config(render_config) {
        LOG_INFO("create frame manager");
        m_frame_manager = std::make_unique<FrameManager>(
            context.get_device(), render_config.max_frames_in_flight);

        LOG_INFO("create per-frame uniform buffers");
        const uint32_t frame_slot_count = m_frame_manager->get_frame_slot_count();
        m_view_project_uniform_buffers.reserve(frame_slot_count);
        for(uint32_t index = 0; index < frame_slot_count; ++index) {
            m_view_project_uniform_buffers.push_back(Buffer::create_cpu_buffer(
                context.get_device(),
                Flags<BufferUsage>(BufferUsage::Uniform),
                sizeof(ViewProjectMatrix),
                nullptr));
        }
    }

    void SceneRenderer::setup_render_pass() {
        LOG_INFO("create render pass");

        const auto surface_format = static_cast<Format>(m_vulkan_config.surface_format);
        const auto depth_format = static_cast<Format>(m_vulkan_config.depth_format);
        const auto msaa_samples = static_cast<SampleCount>(m_vulkan_config.msaa_samples);

        std::vector<Attachment> attachments;
        attachments.emplace_back(Attachment::get_color_attachment(surface_format, msaa_samples));
        attachments.emplace_back(Attachment::get_depth_attachment(depth_format, msaa_samples));

        std::vector<RenderSubPass> render_sub_passes;
        RenderSubPass render_sub_pass_0 = {
            {},
            {SubpassColorAttachment(0)},
            {SubpassDepthStencilAttachment(1)},
            msaa_samples
        };
        render_sub_passes.emplace_back(render_sub_pass_0);

        m_render_pass = std::make_shared<RenderPass>(
            m_context.get_device(), attachments, render_sub_passes, surface_format);

        LOG_INFO("create render pipeline manager");
        m_pipeline_manager = std::make_unique<PipelineManager>(
            m_context.get_device(), m_render_pass.get());

        LOG_INFO("create render target");
        m_render_target = RenderTarget::create_swapchain_target(
            m_context.get_device(), m_render_pass.get(), m_context.get_swapchain());

        const Math::Vec4 clear_color(
            m_render_config.clear_color[0],
            m_render_config.clear_color[1],
            m_render_config.clear_color[2],
            m_render_config.clear_color[3]
        );
        m_render_target->set_clear_value(ClearValue(clear_color));

        const auto image_count = static_cast<uint32_t>(
            m_context.get_swapchain()->get_images().size());
        m_frame_manager->initialize_swapchain_images(image_count);

        m_render_mode = RenderMode::Runtime;
    }

    std::shared_ptr<DescriptorSetLayout> SceneRenderer::create_descriptor_set_layout(const DescriptorSetLayoutBindings& bindings) {
        if(!m_descriptor_set_layout) {
            m_descriptor_set_layout = std::make_shared<DescriptorSetLayout>(
                m_context.get_device(), bindings);
        }
        return m_descriptor_set_layout;
    }

    void SceneRenderer::setup_pipeline(ResourceManager& resource_manager,
                                       const ShaderLayout& layout,
                                       const VertexInputDescription& vertex_input,
                                       const PipelineConfig& config) {
        LOG_INFO("setup pipeline");

        // 创建着色器
        const auto vert_shader = resource_manager.get_shader_manager().load_shader(
            "cube_texture_vert", CUBE_TEXTURE_VERT, layout);
        const auto frag_shader = resource_manager.get_shader_manager().load_shader(
            "cube_texture_frag", CUBE_TEXTURE_FRAG, layout);
        m_default_sampler = resource_manager.get_sampler_manager().get_linear_repeat();

        // 创建 Pipeline
        m_pipeline = m_pipeline_manager->create_pipeline(
            "cube_pipeline", layout, vertex_input, config, vert_shader, frag_shader);
    }

    const DescriptorSet& SceneRenderer::prepare_material_descriptor_set(
        const MaterialBinding& material,
        const std::shared_ptr<Buffer>& view_project_buffer,
        const Sampler& sampler) {
        if(!m_descriptor_set_layout) {
            LOG_FATAL("Descriptor set layout must be created before preparing material descriptors");
        }

        auto [iterator, inserted] =
                m_material_descriptors.try_emplace(material.material_handle);
        MaterialDescriptorState& state = iterator->second;
        if(inserted) {
            const uint32_t frame_slot_count = m_frame_manager->get_frame_slot_count();
            DescriptorPoolSizes descriptor_pool_sizes;
            descriptor_pool_sizes.add_pool_size(
                DescriptorType::UniformBuffer, frame_slot_count);
            descriptor_pool_sizes.add_pool_size(
                DescriptorType::CombinedImageSampler, 2 * frame_slot_count);
            state.pool = std::make_shared<DescriptorPool>(
                m_context.get_device(), frame_slot_count, descriptor_pool_sizes);
            state.descriptor_sets = state.pool->allocate_descriptor_set(
                *m_descriptor_set_layout, frame_slot_count);
            state.resources.resize(frame_slot_count);
        }

        const uint32_t frame_slot_index =
                m_frame_manager->get_current_frame_slot_index();
        const DescriptorSet& descriptor_set =
                state.descriptor_sets.at(frame_slot_index);
        DescriptorResources& current_resources = state.resources.at(frame_slot_index);
        if(current_resources.view_project_buffer != view_project_buffer
           || current_resources.textures != material.textures) {
            const DescriptorResources resources = {
                .view_project_buffer = view_project_buffer,
                .textures = material.textures
            };
            update_descriptor_set(descriptor_set, resources, sampler);
            current_resources = resources;
        }
        return descriptor_set;
    }

    void SceneRenderer::render(const RenderSubmission& submission,
                               const ViewProjectMatrix& view_project_matrix) {
        PROFILE_SCOPE("SceneRenderer::render");

        if(!m_pipeline || !m_default_sampler) {
            LOG_ERROR("SceneRenderer resources are not set up. Call setup_pipeline() first.");
            return;
        }

        const uint32_t frame_slot_index =
                m_frame_manager->get_current_frame_slot_index();
        const auto& view_project_buffer =
                m_view_project_uniform_buffers.at(frame_slot_index);
        std::static_pointer_cast<CPUBuffer>(view_project_buffer)->write(
            &view_project_matrix);

        const auto& command_buffer = m_frame_manager->get_current_command_buffer();
        command_buffer.bind_pipeline(*m_pipeline);

        const auto size = m_render_target->get_size();
        command_buffer.set_viewport(Graphics::get_viewport(
            static_cast<float>(size.x), static_cast<float>(size.y)));
        command_buffer.set_scissor(Graphics::get_scissor(
            static_cast<float>(size.x), static_cast<float>(size.y)));

        for(const ResolvedRenderItem& item: submission.render_items) {
            const DescriptorSet& descriptor_set = prepare_material_descriptor_set(
                item.material, view_project_buffer, *m_default_sampler);
            render_item(item, descriptor_set);
        }
    }

    uint32_t SceneRenderer::begin_frame() {
        PROFILE_SCOPE("SceneRenderer::begin_frame");
        m_frame_manager->begin_frame();

        // 暂时所有模式都使用 swapchain
        const auto swapchain = m_context.get_swapchain();
        auto& frame_slot = m_frame_manager->get_current_frame_slot();

        // Acquire next image
        auto [image_index, acquire_result] =
                swapchain->acquire_next_image(frame_slot.image_available_semaphore);
        if(acquire_result == vk::Result::eErrorOutOfDateKHR) {
            recreate_swapchain();
            std::tie(image_index, acquire_result) =
                    swapchain->acquire_next_image(frame_slot.image_available_semaphore);
            if(acquire_result != vk::Result::eSuccess && acquire_result != vk::Result::eSuboptimalKHR) {
                LOG_FATAL("can't acquire swapchain image");
            }
        }

        m_frame_manager->prepare_image(image_index);
        auto& command_buffer = m_frame_manager->get_current_command_buffer();
        command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        m_render_target->begin_render_target(command_buffer);

        return image_index;
    }

    void SceneRenderer::render_item(const ResolvedRenderItem& render_item,
                                    const DescriptorSet& descriptor_set) const {
        PROFILE_SCOPE("SceneRenderer::render_item");

        const auto& command_buffer =
                m_frame_manager->get_current_command_buffer();

        // Bind descriptor sets
        const vk::DescriptorSet vk_descriptor_set = descriptor_set.get();
        command_buffer.get().bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            m_pipeline->get_layout()->get(),
            0,
            1,
            &vk_descriptor_set,
            0,
            nullptr);

        const PushConstant push_constant{.model = render_item.model_matrix};
        command_buffer.push_constants(
            *m_pipeline->get_layout(),
            Flags<ShaderStage>(ShaderStage::Vertex),
            0,
            &push_constant,
            sizeof(push_constant));

        // Draw
        render_item.mesh->draw(command_buffer);
    }

    void SceneRenderer::end_frame() {
        PROFILE_SCOPE("SceneRenderer::end_frame");

        // 暂时所有模式都使用 swapchain
        const auto device = m_context.get_device();
        const auto swapchain = m_context.get_swapchain();
        const uint32_t image_index = swapchain->get_current_index();
        auto& frame_slot = m_frame_manager->get_current_frame_slot();
        auto& image_state =
                m_frame_manager->get_swapchain_image_state(image_index);

        // End command buffer
        frame_slot.command_buffer.end();

        // Submit
        const auto& graphics_queue = device->get_graphics_queue(0);
        graphics_queue.submit(std::span(&frame_slot.command_buffer, 1),
            std::span(&frame_slot.image_available_semaphore, 1),
            std::span(&image_state.render_finished_semaphore, 1),
            &frame_slot.in_flight_fence);

        // Present
        auto& present_queue = device->get_present_queue(0);
        const auto result = present_queue.present(*swapchain,
            std::span(&image_state.render_finished_semaphore, 1), image_index);
        if(result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR) {
            recreate_swapchain();
        } else if(result != vk::Result::eSuccess) {
            LOG_FATAL("failed to present swapchain image: {}", vk::to_string(result));
        }

        m_frame_manager->end_frame();
    }

    void SceneRenderer::end_render_pass() const {
        m_render_target->end_render_target(
            m_frame_manager->get_current_command_buffer());
    }

    void SceneRenderer::set_render_mode(RenderMode mode) {
        if(m_render_mode == mode) {
            return;
        }

        [[maybe_unused]] const RenderMode old_mode = m_render_mode;
        m_render_mode = mode;

        LOG_INFO("SceneRenderer render mode changed: {} -> {}",
            static_cast<int>(old_mode),
            static_cast<int>(mode));

        // 暂时只更新模式变量，不做其他操作
        // 离屏渲染功能后续实现时再添加具体逻辑
    }

    CommandBuffer& SceneRenderer::get_current_command_buffer() const {
        return m_frame_manager->get_current_command_buffer();
    }

    void SceneRenderer::recreate_swapchain() {
        PROFILE_SCOPE("SceneRenderer::recreate_swapchain");
        const auto device = m_context.get_device();
        const auto swapchain = m_context.get_swapchain();

        device->wait_idle();
        swapchain->recreate();

        // Recreate render target with new swapchain
        m_render_target = RenderTarget::create_swapchain_target(
            m_context.get_device(), m_render_pass.get(), m_context.get_swapchain());

        const Math::Vec4 clear_color(
            m_render_config.clear_color[0],
            m_render_config.clear_color[1],
            m_render_config.clear_color[2],
            m_render_config.clear_color[3]
        );
        m_render_target->set_clear_value(ClearValue(clear_color));

        const auto image_count =
                static_cast<uint32_t>(swapchain->get_images().size());
        m_frame_manager->initialize_swapchain_images(image_count);

        if(m_swapchain_recreate_callback) {
            m_swapchain_recreate_callback();
        }
    }

    void SceneRenderer::update_descriptor_set(const DescriptorSet& descriptor_set,
                                              const DescriptorResources& resources,
                                              const Sampler& sampler) const {
        vk::DescriptorBufferInfo buffer_info{};
        buffer_info.buffer = resources.view_project_buffer->get();
        buffer_info.offset = 0;
        buffer_info.range = sizeof(ViewProjectMatrix);

        vk::DescriptorImageInfo image_info0{};
        image_info0.sampler = sampler.get();
        image_info0.imageView = resources.textures[0]->get_image_view()->get();
        image_info0.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorImageInfo image_info1{};
        image_info1.sampler = sampler.get();
        image_info1.imageView = resources.textures[1]->get_image_view()->get();
        image_info1.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        std::vector<vk::WriteDescriptorSet> write_sets;

        vk::WriteDescriptorSet view_project_write{};
        view_project_write.dstSet = descriptor_set.get();
        view_project_write.dstBinding = 0;
        view_project_write.dstArrayElement = 0;
        view_project_write.descriptorType = vk::DescriptorType::eUniformBuffer;
        view_project_write.descriptorCount = 1;
        view_project_write.pBufferInfo = &buffer_info;
        write_sets.emplace_back(view_project_write);

        vk::WriteDescriptorSet texture0_write{};
        texture0_write.dstSet = descriptor_set.get();
        texture0_write.dstBinding = 2;
        texture0_write.dstArrayElement = 0;
        texture0_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        texture0_write.descriptorCount = 1;
        texture0_write.pImageInfo = &image_info0;
        write_sets.emplace_back(texture0_write);

        vk::WriteDescriptorSet texture1_write{};
        texture1_write.dstSet = descriptor_set.get();
        texture1_write.dstBinding = 3;
        texture1_write.dstArrayElement = 0;
        texture1_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        texture1_write.descriptorCount = 1;
        texture1_write.pImageInfo = &image_info1;
        write_sets.emplace_back(texture1_write);

        m_context.get_device()->get().updateDescriptorSets(write_sets, {});
    }
}
