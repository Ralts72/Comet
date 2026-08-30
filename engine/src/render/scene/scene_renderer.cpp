#include "render/scene/scene_renderer.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "render/scene/render_types.h"
#include "cube_texture_frag.h"
#include "cube_texture_vert.h"
#include "graphics/queue.h"
#include "graphics/resource/image_view.h"
#include "graphics/vk_common.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/sampler.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/attachment.h"
#include "graphics/pipeline/vertex_description.h"
#include "render/resource/resource_manager.h"

#include <algorithm>

namespace Comet {
    namespace {
        void append_resource_wait(
            std::vector<QueueSemaphoreSubmit>& waits,
            const GpuCompletionPoint& completion,
            const Flags<PipelineStage> stages) {
            if(!completion.is_valid()) {
                return;
            }

            const QueueSemaphoreSubmit candidate(
                completion,
                stages);
            const auto existing = std::find_if(
                waits.begin(),
                waits.end(),
                [&candidate](const QueueSemaphoreSubmit& wait) {
                    return wait.semaphore == candidate.semaphore;
                });
            if(existing == waits.end()) {
                waits.push_back(candidate);
                return;
            }

            existing->value = std::max(existing->value, candidate.value);
            existing->stage_mask =
                existing->stage_mask | candidate.stage_mask;
        }

        void remove_completed_resource_waits(
            std::vector<QueueSemaphoreSubmit>& waits) {
            std::erase_if(
                waits,
                [](const QueueSemaphoreSubmit& wait) {
                    return wait.semaphore->get_counter_value() >= wait.value;
                });
        }
    }

    SceneRenderer::SceneRenderer(RenderContext& context,
                                 const Config::Vulkan& vulkan_config,
                                 const Config::Render& render_config)
        : m_context(context),
          m_surface_format(vulkan_config.surface_format),
          m_depth_format(vulkan_config.depth_format),
          m_msaa_samples(vulkan_config.msaa_samples),
          m_color_clear_value(Math::Vec4(
              render_config.clear_color[0],
              render_config.clear_color[1],
              render_config.clear_color[2],
              render_config.clear_color[3])) {
        LOG_INFO("create frame manager");
        m_frame_scheduler = std::make_unique<FrameScheduler>(
            context.get_device(), render_config.max_frames_in_flight);

        LOG_INFO("create per-frame uniform buffers");
        const uint32_t frame_slot_count =
            m_frame_scheduler->get_frame_slot_count();
        m_view_project_uniform_buffers.reserve(frame_slot_count);
        for(uint32_t index = 0; index < frame_slot_count; ++index) {
            m_view_project_uniform_buffers.push_back(Buffer::create_cpu_buffer(
                context.get_device(),
                Flags<BufferUsage>(BufferUsage::Uniform),
                sizeof(ViewProjectMatrix),
                nullptr,
                "view-project uniform buffer"));
        }
    }

    void SceneRenderer::setup_render_pass() {
        LOG_INFO("create render pass");

        reset_render_pipeline();

        std::vector<Attachment> attachments;
        attachments.emplace_back(Attachment::get_color_attachment(m_surface_format, m_msaa_samples));
        attachments.emplace_back(Attachment::get_depth_attachment(m_depth_format, m_msaa_samples));

        std::vector<RenderSubPass> render_sub_passes;
        RenderSubPass render_sub_pass_0 = {
            {},
            {SubpassColorAttachment(0)},
            {SubpassDepthStencilAttachment(1)},
            m_msaa_samples
        };
        render_sub_passes.emplace_back(render_sub_pass_0);

        m_render_pass = std::make_shared<RenderPass>(
            m_context.get_device(), attachments, render_sub_passes, m_surface_format);

        LOG_INFO("create render pipeline manager");
        m_pipeline_manager = std::make_unique<PipelineManager>(
            m_context.get_device(), *m_render_pass);

        LOG_INFO("create render target");
        m_render_target = RenderTarget::create_swapchain_target(
            m_context.get_device(), *m_render_pass, m_context.get_swapchain());
        set_render_target_clear_color();

        const auto image_count = static_cast<uint32_t>(
            m_context.get_swapchain().get_images().size());
        m_frame_scheduler->initialize_swapchain_images(image_count);

        m_uses_viewport_target = false;
        m_requested_viewport_size = Math::Vec2u(0);
        m_viewport_size_stable_frames = 0;
    }

    void SceneRenderer::setup_viewport_render_pass(const Math::Vec2u size) {
        if(size.x == 0 || size.y == 0) {
            LOG_FATAL("Viewport render target size must be greater than zero");
        }

        LOG_INFO("create viewport render pass at {}x{}", size.x, size.y);
        reset_render_pipeline();

        Attachment color_attachment = Attachment::get_color_attachment(
            m_surface_format, m_msaa_samples);
        if(m_msaa_samples == SampleCount::Count1) {
            color_attachment.description.store_op = AttachmentStoreOp::Store;
            color_attachment.description.final_layout = ImageLayout::ShaderReadOnlyOptimal;
            color_attachment.usage |= ImageUsage::Sampled;
        }

        std::vector<Attachment> attachments;
        attachments.emplace_back(color_attachment);
        attachments.emplace_back(Attachment::get_depth_attachment(m_depth_format, m_msaa_samples));

        RenderSubPass render_sub_pass = {
            {},
            {SubpassColorAttachment(0)},
            {SubpassDepthStencilAttachment(1)},
            m_msaa_samples
        };
        render_sub_pass.resolve_final_layout = ImageLayout::ShaderReadOnlyOptimal;
        render_sub_pass.resolve_usage =
            Flags<ImageUsage>(ImageUsage::ColorAttachment) | ImageUsage::Sampled;

        m_render_pass = std::make_shared<RenderPass>(
            m_context.get_device(), attachments,
            std::vector<RenderSubPass>{render_sub_pass}, m_surface_format);
        m_pipeline_manager = std::make_unique<PipelineManager>(
            m_context.get_device(), *m_render_pass);
        m_render_target = RenderTarget::create_multi_target(
            m_context.get_device(), *m_render_pass, size,
            m_frame_scheduler->get_frame_slot_count());
        set_render_target_clear_color();

        m_uses_viewport_target = true;
        m_requested_viewport_size = size;
        m_viewport_size_stable_frames = 0;
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
                                       const PipelineConfig& config) {
        LOG_INFO("setup pipeline");

        // 创建着色器
        const auto vert_shader = resource_manager.get_shader_manager().load_shader(
            "cube_texture_vert", CUBE_TEXTURE_VERT);
        const auto frag_shader = resource_manager.get_shader_manager().load_shader(
            "cube_texture_frag", CUBE_TEXTURE_FRAG);
        m_default_sampler = resource_manager.get_sampler_manager().get_linear_repeat();

        // 创建 Pipeline
        m_pipeline = m_pipeline_manager->create_pipeline(
            "cube_pipeline", layout, config, vert_shader, frag_shader);
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
        state.last_used_frame_serial =
            m_frame_scheduler->get_current_frame_serial();
        if(inserted) {
            const uint32_t frame_slot_count =
                m_frame_scheduler->get_frame_slot_count();
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
                m_frame_scheduler->get_current_frame_slot_index();
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

    std::vector<QueueSemaphoreSubmit> SceneRenderer::render(
        const RenderSubmission& submission) {
        PROFILE_SCOPE("SceneRenderer::render");

        if(!submission.view_project_matrix) return {};

        if(!m_pipeline || !m_default_sampler) {
            LOG_ERROR("SceneRenderer resources are not set up. Call setup_pipeline() first.");
            return {};
        }

        const uint32_t frame_slot_index =
                m_frame_scheduler->get_current_frame_slot_index();
        const auto& view_project_buffer =
                m_view_project_uniform_buffers.at(frame_slot_index);
        std::static_pointer_cast<CPUBuffer>(view_project_buffer)->write(
            &*submission.view_project_matrix);

        const auto& command_buffer =
            m_frame_scheduler->get_current_command_buffer();
        command_buffer.bind_pipeline(*m_pipeline);

        const auto size = m_render_target->get_size();
        command_buffer.set_viewport(Graphics::get_viewport(
            static_cast<float>(size.x), static_cast<float>(size.y)));
        command_buffer.set_scissor(Graphics::get_scissor(
            static_cast<float>(size.x), static_cast<float>(size.y)));

        std::vector<QueueSemaphoreSubmit> resource_waits;
        const auto retain_resource = [this](const auto& resource) {
            if(resource
               && m_recorded_resource_ids.insert(resource.get()).second) {
                m_recorded_resource_owners.emplace_back(resource);
            }
        };
        for(const ResolvedRenderItem& item: submission.render_items) {
            retain_resource(item.mesh);
            append_resource_wait(
                resource_waits,
                item.mesh->get_ready_completion(),
                Flags<PipelineStage>(PipelineStage::VertexInput));
            for(const auto& texture: item.material.textures) {
                retain_resource(texture);
                append_resource_wait(
                    resource_waits,
                    texture->get_ready_completion(),
                    Flags<PipelineStage>(PipelineStage::FragmentShader));
            }
            const DescriptorSet& descriptor_set = prepare_material_descriptor_set(
                item.material, view_project_buffer, *m_default_sampler);
            render_item(item, descriptor_set);
        }
        remove_completed_resource_waits(resource_waits);
        return resource_waits;
    }

    bool SceneRenderer::begin_frame() {
        PROFILE_SCOPE("SceneRenderer::begin_frame");
        if(!m_recorded_resource_owners.empty()
           || !m_recorded_resource_ids.empty()) {
            LOG_FATAL("Previous frame resources were not retired");
        }
        m_frame_scheduler->wait_for_current_slot();
        m_context.get_device().set_allocator_frame_index(
            m_frame_scheduler->get_current_frame_serial());
        m_retirement_queue.collect_completed();
        collect_completed_material_descriptors();
        apply_pending_viewport_resize();

        auto& swapchain = m_context.get_swapchain();
        auto& frame_slot = m_frame_scheduler->get_current_frame_slot();

        // Acquire next image
        auto [image_index, acquire_result] =
                swapchain.acquire_next_image(frame_slot.image_available_semaphore);
        if(acquire_result == vk::Result::eErrorOutOfDateKHR) {
            if(!recreate_swapchain()) {
                return false;
            }
            std::tie(image_index, acquire_result) =
                    swapchain.acquire_next_image(frame_slot.image_available_semaphore);
            if(acquire_result != vk::Result::eSuccess && acquire_result != vk::Result::eSuboptimalKHR) {
                LOG_FATAL("can't acquire swapchain image");
            }
        }

        m_frame_scheduler->begin_frame(image_index);
        auto& command_buffer =
            m_frame_scheduler->get_current_command_buffer();
        command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        if(m_uses_viewport_target) {
            m_render_target->begin_render_target(
                command_buffer,
                m_frame_scheduler->get_current_frame_slot_index());
        } else {
            m_render_target->begin_render_target(command_buffer);
        }

        return true;
    }

    void SceneRenderer::render_item(const ResolvedRenderItem& render_item,
                                    const DescriptorSet& descriptor_set) const {
        PROFILE_SCOPE("SceneRenderer::render_item");

        const auto& command_buffer =
                m_frame_scheduler->get_current_command_buffer();

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

    void SceneRenderer::end_frame(
        const std::span<const QueueSemaphoreSubmit> resource_waits) {
        PROFILE_SCOPE("SceneRenderer::end_frame");

        auto& device = m_context.get_device();
        auto& swapchain = m_context.get_swapchain();
        const uint32_t image_index = swapchain.get_current_index();
        auto& frame_slot = m_frame_scheduler->get_current_frame_slot();
        auto& image_state =
                m_frame_scheduler->get_swapchain_image_state(image_index);

        // End command buffer
        frame_slot.command_buffer.end();

        // Submit
        auto& graphics_queue = device.get_graphics_queue(0);
        std::vector<QueueSemaphoreSubmit> waits;
        waits.reserve(1 + resource_waits.size());
        waits.emplace_back(QueueSemaphoreSubmit{
            frame_slot.image_available_semaphore,
            Flags<PipelineStage>(PipelineStage::ColorAttachmentOutput)
        });
        waits.insert(waits.end(), resource_waits.begin(), resource_waits.end());
        const QueueSemaphoreSubmit render_finished_signal{
            image_state.render_finished_semaphore,
            Flags<PipelineStage>(PipelineStage::AllCommands)
        };
        const auto completion = graphics_queue.submit2(
            waits,
            std::span(&frame_slot.command_buffer, 1),
            std::span(&render_finished_signal, 1),
            &frame_slot.in_flight_fence);
        m_frame_scheduler->record_submission(completion);
        m_retirement_queue.retire_batch(
            completion,
            std::move(m_recorded_resource_owners));
        m_recorded_resource_ids.clear();

        // Present
        auto& present_queue = device.get_present_queue(0);
        const auto result = present_queue.present(swapchain,
            std::span(&image_state.render_finished_semaphore, 1), image_index);
        if(result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR) {
            static_cast<void>(recreate_swapchain());
        } else if(result != vk::Result::eSuccess) {
            LOG_FATAL("failed to present swapchain image: {}", vk::to_string(result));
        }

        m_frame_scheduler->end_frame();
    }

    void SceneRenderer::end_render_pass() const {
        m_render_target->end_render_target(
            m_frame_scheduler->get_current_command_buffer());
    }

    void SceneRenderer::request_viewport_resize(const Math::Vec2u size) {
        if(!m_uses_viewport_target || size.x == 0 || size.y == 0) {
            return;
        }

        if(size != m_requested_viewport_size) {
            m_requested_viewport_size = size;
            m_viewport_size_stable_frames = 0;
            return;
        }

        if(m_viewport_size_stable_frames < 2) {
            ++m_viewport_size_stable_frames;
        }
    }

    CommandBuffer& SceneRenderer::get_current_command_buffer() const {
        return m_frame_scheduler->get_current_command_buffer();
    }

    std::vector<std::shared_ptr<ImageView>> SceneRenderer::get_viewport_color_views() const {
        std::vector<std::shared_ptr<ImageView>> color_views;
        if(!m_uses_viewport_target) {
            return color_views;
        }

        const uint32_t frame_slot_count =
            m_frame_scheduler->get_frame_slot_count();
        color_views.reserve(frame_slot_count);
        for(uint32_t index = 0; index < frame_slot_count; ++index) {
            color_views.push_back(m_render_target->get_color_view(index));
        }
        return color_views;
    }

    bool SceneRenderer::recreate_swapchain() {
        PROFILE_SCOPE("SceneRenderer::recreate_swapchain");
        auto& swapchain = m_context.get_swapchain();

        if(!swapchain.recreate()) {
            return false;
        }

        if(!m_uses_viewport_target) {
            m_render_target = RenderTarget::create_swapchain_target(
                m_context.get_device(), *m_render_pass, m_context.get_swapchain());
            set_render_target_clear_color();
        }

        const auto image_count =
                static_cast<uint32_t>(swapchain.get_images().size());
        m_frame_scheduler->initialize_swapchain_images(image_count);

        if(m_swapchain_recreate_callback) {
            m_swapchain_recreate_callback();
        }
        return true;
    }

    void SceneRenderer::reset_render_pipeline() {
        m_pipeline.reset();
        m_pipeline_manager.reset();
        m_render_target.reset();
        m_render_pass.reset();
    }

    void SceneRenderer::collect_completed_material_descriptors() {
        std::erase_if(
            m_material_descriptors,
            [this](const auto& entry) {
                return m_frame_scheduler->is_frame_serial_complete(
                    entry.second.last_used_frame_serial);
            });
    }

    void SceneRenderer::set_render_target_clear_color() const {
        m_render_target->set_clear_value(m_color_clear_value);
    }

    void SceneRenderer::apply_pending_viewport_resize() {
        if(!m_uses_viewport_target
           || m_viewport_size_stable_frames < 2
           || m_render_target->get_size() == m_requested_viewport_size) {
            return;
        }

        m_context.wait_idle();
        m_render_target->resize(
            m_requested_viewport_size.x, m_requested_viewport_size.y);
        m_viewport_size_stable_frames = 0;
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

        m_context.get_device().get().updateDescriptorSets(write_sets, {});
    }
}
