#include "render/scene/scene_renderer.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "render/scene/render_types.h"
#include "graphics/convert.h"
#include "graphics/queue.h"
#include "graphics/vk_common.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/attachment.h"
#include "render/resource/resource_manager.h"

#include <algorithm>
#include <utility>
#include <stdexcept>

namespace Comet {
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

    void SceneRenderer::setup_pipeline(ResourceManager& resource_manager) {
        m_material_renderer = std::make_unique<MaterialRenderer>(m_context.get_device(),
            *m_pipeline_manager, resource_manager,
            m_frame_scheduler->get_frame_slot_count(), m_msaa_samples);
        m_debug_renderer = std::make_unique<DebugRenderer>(m_context.get_device(),
            *m_pipeline_manager, resource_manager,
            m_frame_scheduler->get_frame_slot_count(), m_msaa_samples);
    }

    MaterialRenderer::ReloadReport SceneRenderer::reload_material_shaders(
        ResourceManager& resources, const ShaderManager::Bytecodes& bytecodes) {
        if(m_frame_scheduler->is_frame_active())
            throw std::logic_error("Shader publication requires a frame boundary");
        if(!m_material_renderer || !m_pipeline_manager)
            throw std::logic_error("Material renderer is not initialized");
        return m_material_renderer->reload_shaders(*m_pipeline_manager,
            resources.get_shader_manager(), bytecodes, m_msaa_samples);
    }

    std::vector<std::shared_ptr<const MaterialLayout>> SceneRenderer::
        get_material_layouts() const {
        if(!m_material_renderer)
            return {};
        return m_material_renderer->get_material_layouts();
    }

    bool SceneRenderer::reload_debug_shaders(
        ResourceManager& resources, const ShaderManager::Bytecodes& bytecodes) {
        if(m_frame_scheduler->is_frame_active())
            throw std::logic_error("Shader publication requires a frame boundary");
        if(!m_debug_renderer || !m_pipeline_manager)
            throw std::logic_error("Debug renderer is not initialized");
        return m_debug_renderer->reload_shaders(*m_pipeline_manager,
            resources.get_shader_manager(), bytecodes, m_msaa_samples);
    }

    std::vector<QueueSemaphoreSubmit> SceneRenderer::render_scene_pass(
        const RenderSubmission& submission, const LineDrawList& lines) {
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
            const auto size = m_render_target->get_size();
            command_buffer.set_viewport(Graphics::get_viewport(
                static_cast<float>(size.x), static_cast<float>(size.y)));
            command_buffer.set_scissor(Graphics::get_scissor(
                static_cast<float>(size.x), static_cast<float>(size.y)));
            if(m_material_renderer) {
                resource_waits = m_material_renderer->render(*m_frame_scheduler,
                    *submission.view_project_matrix, submission.render_items);
            }
            if(m_debug_renderer) {
                m_debug_renderer->render(
                    *m_frame_scheduler, *submission.view_project_matrix, lines);
            }
        }

        m_render_target->end_render_target(command_buffer);
        return resource_waits;
    }

    bool SceneRenderer::begin_frame() {
        PROFILE_SCOPE("SceneRenderer::begin_frame");
        m_frame_scheduler->wait_for_current_slot();
        m_context.get_device().set_allocator_frame_index(
            m_frame_scheduler->get_current_frame_serial());

        auto& swapchain = m_context.get_swapchain();
        auto& frame_slot = m_frame_scheduler->get_current_frame_slot();

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

    void SceneRenderer::end_frame(
        const std::span<const QueueSemaphoreSubmit> resource_waits) {
        PROFILE_SCOPE("SceneRenderer::end_frame");

        auto& device = m_context.get_device();
        auto& swapchain = m_context.get_swapchain();
        const uint32_t image_index = swapchain.get_current_index();
        auto& frame_slot = m_frame_scheduler->get_current_frame_slot();
        auto& image_state = m_frame_scheduler->get_swapchain_image_state(image_index);

        frame_slot.command_buffer.end();

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
        m_debug_renderer.reset();
        m_material_renderer.reset();
        m_pipeline_manager.reset();
        m_render_target.reset();
        m_render_pass.reset();
    }

    void SceneRenderer::set_render_target_clear_color() const {
        m_render_target->set_clear_value(m_color_clear_value);
    }

}
