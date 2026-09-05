#include "render/scene/scene_renderer.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "render/scene/render_types.h"
#include "cube_texture_frag.h"
#include "cube_texture_vert.h"
#include "graphics/convert.h"
#include "graphics/queue.h"
#include "graphics/resource/image_view.h"
#include "graphics/vk_common.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/sampler.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/attachment.h"
#include "graphics/pipeline/vertex_description.h"
#include "render/resource/mesh_data.h"
#include "render/resource/resource_manager.h"

#include <algorithm>
#include <utility>

namespace Comet {
    namespace {
        void append_resource_wait(std::vector<QueueSemaphoreSubmit>& waits,
            const GpuCompletionPoint& completion, const Flags<PipelineStage> stages) {
            if(!completion.is_valid()) {
                return;
            }

            const QueueSemaphoreSubmit candidate(completion, stages);
            const auto existing = std::find_if(waits.begin(), waits.end(),
                [&candidate](const QueueSemaphoreSubmit& wait) {
                    return wait.semaphore == candidate.semaphore;
                });
            if(existing == waits.end()) {
                waits.push_back(candidate);
                return;
            }

            existing->value = std::max(existing->value, candidate.value);
            existing->stage_mask = existing->stage_mask | candidate.stage_mask;
        }

        void remove_completed_resource_waits(std::vector<QueueSemaphoreSubmit>& waits) {
            std::erase_if(waits, [](const QueueSemaphoreSubmit& wait) {
                return wait.semaphore->get_counter_value() >= wait.value;
            });
        }
    }

    SceneRenderer::SceneRenderer(RenderContext& context,
        const Config::Vulkan& vulkan_config, const Config::Render& render_config)
        : m_context(context),
          m_surface_format(Graphics::vk_to_format(context.get_swapchain()
                  .get_active_generation()
                  ->get_config()
                  .surface_format.format)),
          m_depth_format(vulkan_config.depth_format),
          m_msaa_samples(vulkan_config.msaa_samples),
          m_color_clear_value(
              Math::Vec4(render_config.clear_color[0], render_config.clear_color[1],
                  render_config.clear_color[2], render_config.clear_color[3])) {
        LOG_INFO("create frame scheduler");
        m_frame_scheduler = std::make_unique<FrameScheduler>(
            context.get_device(), render_config.max_frames_in_flight);

        LOG_INFO("create per-frame uniform buffers");
        const uint32_t frame_slot_count = m_frame_scheduler->get_frame_slot_count();
        m_view_project_uniform_buffers.reserve(frame_slot_count);
        for(uint32_t index = 0; index < frame_slot_count; ++index) {
            m_view_project_uniform_buffers.push_back(Buffer::create_cpu_buffer(
                context.get_device(), Flags<BufferUsage>(BufferUsage::Uniform),
                sizeof(ViewProjectMatrix), nullptr, "view-project uniform buffer"));
        }
    }

    void SceneRenderer::set_swapchain_resource_callbacks(
        SwapchainReleaseCallback release_resources,
        SwapchainRebuildCallback rebuild_resources) {
        m_release_swapchain_resources = std::move(release_resources);
        m_rebuild_swapchain_resources = std::move(rebuild_resources);
    }

    void SceneRenderer::setup_render_pass() {
        LOG_INFO("create render pass");

        reset_render_pipeline();

        std::vector<Attachment> attachments;
        attachments.emplace_back(
            Attachment::get_color_attachment(m_surface_format, m_msaa_samples));
        attachments.emplace_back(
            Attachment::get_depth_attachment(m_depth_format, m_msaa_samples));

        std::vector<RenderSubPass> render_sub_passes;
        RenderSubPass render_sub_pass_0 = {{}, {SubpassColorAttachment(0)},
            {SubpassDepthStencilAttachment(1)}, m_msaa_samples};
        render_sub_passes.emplace_back(render_sub_pass_0);

        m_render_pass = std::make_shared<RenderPass>(
            m_context.get_device(), attachments, render_sub_passes, m_surface_format);

        LOG_INFO("create render pipeline manager");
        m_pipeline_manager =
            std::make_unique<PipelineManager>(m_context.get_device(), *m_render_pass);

        LOG_INFO("create render target");
        m_render_target = RenderTarget::create_swapchain_target(
            m_context.get_device(), *m_render_pass, m_context.get_swapchain());
        set_render_target_clear_color();

        const auto image_count =
            static_cast<uint32_t>(m_context.get_swapchain().get_images().size());
        m_frame_scheduler->initialize_swapchain_images(image_count);

        m_uses_offscreen_target = false;
    }

    void SceneRenderer::setup_offscreen_render_pass(const Math::Vec2u size) {
        if(size.x == 0 || size.y == 0) {
            LOG_FATAL("Offscreen render target size must be greater than zero");
        }

        LOG_INFO("create offscreen render pass at {}x{}", size.x, size.y);
        reset_render_pipeline();

        Attachment color_attachment =
            Attachment::get_color_attachment(m_surface_format, m_msaa_samples);
        if(m_msaa_samples == SampleCount::Count1) {
            color_attachment.description.store_op = AttachmentStoreOp::Store;
            color_attachment.description.final_layout =
                ImageLayout::ShaderReadOnlyOptimal;
            color_attachment.usage |= ImageUsage::Sampled;
        }

        std::vector<Attachment> attachments;
        attachments.emplace_back(color_attachment);
        attachments.emplace_back(
            Attachment::get_depth_attachment(m_depth_format, m_msaa_samples));

        RenderSubPass render_sub_pass = {{}, {SubpassColorAttachment(0)},
            {SubpassDepthStencilAttachment(1)}, m_msaa_samples};
        render_sub_pass.resolve_final_layout = ImageLayout::ShaderReadOnlyOptimal;
        render_sub_pass.resolve_usage =
            Flags<ImageUsage>(ImageUsage::ColorAttachment) | ImageUsage::Sampled;

        m_render_pass = std::make_shared<RenderPass>(m_context.get_device(), attachments,
            std::vector<RenderSubPass>{render_sub_pass}, m_surface_format);
        m_pipeline_manager =
            std::make_unique<PipelineManager>(m_context.get_device(), *m_render_pass);
        m_render_target = RenderTarget::create_multi_target(m_context.get_device(),
            *m_render_pass, size, m_frame_scheduler->get_frame_slot_count());
        set_render_target_clear_color();

        m_uses_offscreen_target = true;
    }

    std::shared_ptr<DescriptorSetLayout> SceneRenderer::create_descriptor_set_layout(
        const DescriptorSetLayoutBindings& bindings) {
        if(!m_descriptor_set_layout) {
            m_descriptor_set_layout =
                std::make_shared<DescriptorSetLayout>(m_context.get_device(), bindings);
        }
        return m_descriptor_set_layout;
    }

    void SceneRenderer::setup_pipeline(ResourceManager& resource_manager) {
        LOG_INFO("setup pipeline");

        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(
            0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        bindings.add_binding(2, DescriptorType::CombinedImageSampler,
            Flags<ShaderStage>(ShaderStage::Fragment));
        bindings.add_binding(3, DescriptorType::CombinedImageSampler,
            Flags<ShaderStage>(ShaderStage::Fragment));
        auto descriptor_set_layout = create_descriptor_set_layout(bindings);

        ShaderLayout layout = {};
        layout.descriptor_set_layouts.push_back(descriptor_set_layout);
        layout.push_constants.push_back(std::make_shared<PushConstantRange>(
            ShaderStage::Vertex, 0, sizeof(PushConstant)));

        VertexInputDescription vertex_input_description;
        vertex_input_description.add_binding(
            0, sizeof(MeshVertex), VertexInputRate::Vertex);
        vertex_input_description.add_attribute(
            0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, position));
        vertex_input_description.add_attribute(
            1, 0, Format::R32G32_SFLOAT, offsetof(MeshVertex, texcoord));
        vertex_input_description.add_attribute(
            2, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, normal));

        PipelineConfig pipeline_config = {};
        pipeline_config.set_vertex_input_state(vertex_input_description);
        pipeline_config.set_input_assembly_state(Topology::TriangleList);
        pipeline_config.set_dynamic_state(
            {DynamicState::Viewport, DynamicState::Scissor});
        pipeline_config.enable_depth_test();
        pipeline_config.set_multisample_state(m_msaa_samples, false, 0.2f);

        // 创建着色器
        const auto vert_shader = resource_manager.get_shader_manager().load_shader(
            "cube_texture_vert", CUBE_TEXTURE_VERT);
        const auto frag_shader = resource_manager.get_shader_manager().load_shader(
            "cube_texture_frag", CUBE_TEXTURE_FRAG);
        m_default_sampler = resource_manager.get_sampler_manager().get_linear_repeat();

        // 创建 Pipeline
        m_pipeline = m_pipeline_manager->create_pipeline(
            "cube_pipeline", layout, pipeline_config, vert_shader, frag_shader);
    }

    const DescriptorSet& SceneRenderer::prepare_material_descriptor_set(
        const MaterialBinding& material,
        const std::shared_ptr<Buffer>& view_project_buffer, const Sampler& sampler) {
        if(!m_descriptor_set_layout) {
            LOG_FATAL(
                "Descriptor set layout must be created before preparing material descriptors");
        }

        auto [iterator, inserted] =
            m_material_descriptors.try_emplace(material.material_handle);
        MaterialDescriptorState& state = iterator->second;
        state.last_used_frame_serial = m_frame_scheduler->get_current_frame_serial();
        if(inserted) {
            const uint32_t frame_slot_count = m_frame_scheduler->get_frame_slot_count();
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
        const DescriptorSet& descriptor_set = state.descriptor_sets.at(frame_slot_index);
        DescriptorResources& current_resources = state.resources.at(frame_slot_index);
        if(current_resources.view_project_buffer != view_project_buffer
            || current_resources.textures != material.textures) {
            const DescriptorResources resources = {
                .view_project_buffer = view_project_buffer,
                .textures = material.textures};
            update_descriptor_set(descriptor_set, resources, sampler);
            current_resources = resources;
        }
        return descriptor_set;
    }

    std::vector<QueueSemaphoreSubmit> SceneRenderer::render_scene_pass(
        const RenderSubmission& submission) {
        PROFILE_SCOPE("SceneRenderer::render_scene_pass");

        auto& command_buffer = m_frame_scheduler->get_current_command_buffer();
        if(m_uses_offscreen_target) {
            m_frame_scheduler->retain_current_frame_resource(m_render_target);
            m_render_target->begin_render_target(
                command_buffer, m_frame_scheduler->get_current_frame_slot_index());
        } else {
            m_render_target->begin_render_target(command_buffer);
        }

        std::vector<QueueSemaphoreSubmit> resource_waits;
        if(submission.view_project_matrix) {
            if(!m_pipeline || !m_default_sampler) {
                LOG_ERROR(
                    "SceneRenderer resources are not set up. Call setup_pipeline() first.");
            } else {
                const uint32_t frame_slot_index =
                    m_frame_scheduler->get_current_frame_slot_index();
                const auto& view_project_buffer =
                    m_view_project_uniform_buffers.at(frame_slot_index);
                std::static_pointer_cast<CPUBuffer>(view_project_buffer)
                    ->write(&*submission.view_project_matrix);

                command_buffer.bind_pipeline(*m_pipeline);

                const auto size = m_render_target->get_size();
                command_buffer.set_viewport(Graphics::get_viewport(
                    static_cast<float>(size.x), static_cast<float>(size.y)));
                command_buffer.set_scissor(Graphics::get_scissor(
                    static_cast<float>(size.x), static_cast<float>(size.y)));

                for(const ResolvedRenderItem& item : submission.render_items) {
                    m_frame_scheduler->retain_current_frame_resource(item.mesh);
                    append_resource_wait(resource_waits,
                        item.mesh->get_ready_completion(),
                        Flags<PipelineStage>(PipelineStage::VertexInput));
                    for(const auto& texture : item.material.textures) {
                        m_frame_scheduler->retain_current_frame_resource(texture);
                        append_resource_wait(resource_waits,
                            texture->get_ready_completion(),
                            Flags<PipelineStage>(PipelineStage::FragmentShader));
                    }
                    const DescriptorSet& descriptor_set = prepare_material_descriptor_set(
                        item.material, view_project_buffer, *m_default_sampler);
                    render_item(item, descriptor_set);
                }
                remove_completed_resource_waits(resource_waits);
            }
        }

        m_render_target->end_render_target(command_buffer);
        collect_completed_material_descriptors();
        return resource_waits;
    }

    bool SceneRenderer::begin_frame() {
        PROFILE_SCOPE("SceneRenderer::begin_frame");
        m_frame_scheduler->wait_for_current_slot();
        m_context.get_device().set_allocator_frame_index(
            m_frame_scheduler->get_current_frame_serial());

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
            if(acquire_result != vk::Result::eSuccess
                && acquire_result != vk::Result::eSuboptimalKHR) {
                LOG_FATAL("can't acquire swapchain image");
            }
        }

        m_frame_scheduler->begin_frame(image_index);
        auto& command_buffer = m_frame_scheduler->get_current_command_buffer();
        command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

        return true;
    }

    void SceneRenderer::render_item(const ResolvedRenderItem& render_item,
        const DescriptorSet& descriptor_set) const {
        PROFILE_SCOPE("SceneRenderer::render_item");

        const auto& command_buffer = m_frame_scheduler->get_current_command_buffer();

        // Bind descriptor sets
        const vk::DescriptorSet vk_descriptor_set = descriptor_set.get();
        command_buffer.get().bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
            m_pipeline->get_layout()->get(), 0, 1, &vk_descriptor_set, 0, nullptr);

        const PushConstant push_constant{.model = render_item.model_matrix};
        command_buffer.push_constants(*m_pipeline->get_layout(),
            Flags<ShaderStage>(ShaderStage::Vertex), 0, &push_constant,
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
        auto& image_state = m_frame_scheduler->get_swapchain_image_state(image_index);

        // End command buffer
        frame_slot.command_buffer.end();

        // Submit
        auto& graphics_queue = device.get_graphics_queue(0);
        std::vector<QueueSemaphoreSubmit> waits;
        waits.reserve(1 + resource_waits.size());
        waits.emplace_back(QueueSemaphoreSubmit{frame_slot.image_available_semaphore,
            Flags<PipelineStage>(PipelineStage::ColorAttachmentOutput)});
        waits.insert(waits.end(), resource_waits.begin(), resource_waits.end());
        const QueueSemaphoreSubmit render_finished_signal{
            image_state.render_finished_semaphore,
            Flags<PipelineStage>(PipelineStage::AllCommands)};
        static_cast<void>(
            graphics_queue.submit2(waits, std::span(&frame_slot.command_buffer, 1),
                std::span(&render_finished_signal, 1), &frame_slot.in_flight_fence));
        m_frame_scheduler->record_submission();

        // Present
        auto& present_queue = device.get_present_queue(0);
        const auto result = present_queue.present(
            swapchain, std::span(&image_state.render_finished_semaphore, 1), image_index);
        if(result == vk::Result::eSuboptimalKHR
            || result == vk::Result::eErrorOutOfDateKHR) {
            static_cast<void>(recreate_swapchain());
        } else if(result != vk::Result::eSuccess) {
            LOG_FATAL("failed to present swapchain image: {}", vk::to_string(result));
        }

        m_frame_scheduler->end_frame();
    }

    void SceneRenderer::resize_offscreen_target(const Math::Vec2u size) {
        if(!m_uses_offscreen_target || size.x == 0 || size.y == 0) {
            return;
        }
        if(m_render_target->get_size() == size) {
            return;
        }

        auto candidate = RenderTarget::try_create_multi_target(m_context.get_device(),
            *m_render_pass, size, m_frame_scheduler->get_frame_slot_count());
        if(!candidate) {
            const Math::Vec2u current_size = m_render_target->get_size();
            LOG_ERROR("Keeping offscreen render target at {}x{} after {}x{} generation "
                      "creation failed: {}",
                current_size.x, current_size.y, size.x, size.y,
                vk::to_string(candidate.result()));
            return;
        }

        std::shared_ptr<RenderTarget> next_generation(std::move(candidate).value());
        next_generation->set_clear_value(m_color_clear_value);
        LOG_INFO("Commit offscreen render target generation {}x{}", size.x, size.y);
        m_render_target = std::move(next_generation);
    }

    CommandBuffer& SceneRenderer::get_current_command_buffer() const {
        return m_frame_scheduler->get_current_command_buffer();
    }

    std::shared_ptr<ImageView> SceneRenderer::get_offscreen_color_view(
        const uint32_t frame_slot_index) const {
        if(!m_uses_offscreen_target) {
            return nullptr;
        }

        if(frame_slot_index >= m_frame_scheduler->get_frame_slot_count()) {
            LOG_FATAL("Offscreen frame slot {} exceeds frame slot count {}",
                frame_slot_index, m_frame_scheduler->get_frame_slot_count());
        }
        return m_render_target->get_color_view(frame_slot_index);
    }

    bool SceneRenderer::recreate_swapchain() {
        PROFILE_SCOPE("SceneRenderer::recreate_swapchain");
        auto& swapchain = m_context.get_swapchain();
        const SwapchainConfig previous_config =
            swapchain.get_active_generation()->get_config();

        m_frame_scheduler->wait_for_all_slots();
        m_context.get_device().get_present_queue(0).wait_idle();
        if(!m_uses_offscreen_target) {
            m_render_target.reset();
        }
        if(m_release_swapchain_resources) {
            m_release_swapchain_resources();
        }

        if(!swapchain.recreate()) {
            if(!m_uses_offscreen_target) {
                m_render_target = RenderTarget::create_swapchain_target(
                    m_context.get_device(), *m_render_pass, swapchain);
                set_render_target_clear_color();
            }
            if(m_rebuild_swapchain_resources) {
                m_rebuild_swapchain_resources({});
            }
            return false;
        }

        const SwapchainCompatibility compatibility = compare_swapchain_configs(
            previous_config, swapchain.get_active_generation()->get_config());
        if(!m_uses_offscreen_target && compatibility.format_changed) {
            LOG_FATAL("Runtime swapchain format changed; RenderPass/Pipeline generation "
                      "rebuild is not implemented yet");
        }
        if(!m_uses_offscreen_target) {
            m_render_target = RenderTarget::create_swapchain_target(
                m_context.get_device(), *m_render_pass, swapchain);
            set_render_target_clear_color();
        }

        const auto image_count = static_cast<uint32_t>(swapchain.get_images().size());
        m_frame_scheduler->initialize_swapchain_images(image_count);

        if(m_rebuild_swapchain_resources) {
            m_rebuild_swapchain_resources(compatibility);
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
        std::erase_if(m_material_descriptors, [this](const auto& entry) {
            return m_frame_scheduler->is_frame_serial_complete(
                entry.second.last_used_frame_serial);
        });
    }

    void SceneRenderer::set_render_target_clear_color() const {
        m_render_target->set_clear_value(m_color_clear_value);
    }

    void SceneRenderer::update_descriptor_set(const DescriptorSet& descriptor_set,
        const DescriptorResources& resources, const Sampler& sampler) const {
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
