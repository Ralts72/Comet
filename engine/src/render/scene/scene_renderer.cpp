#include "render/scene/scene_renderer.h"
#include "render/render_context.h"
#include "config/config.h"
#include "graphics/device.h"
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

#include <stdexcept>
#include <utility>

namespace Comet {
    SceneRenderer::SceneRenderer(RenderContext& context, const Config::Vulkan& vulkan_config,
        const Config::Render& render_config)
        : m_context(context),
          m_surface_format(Graphics::vk_to_format(
              context.get_swapchain().get_active_generation()->get_config().surface_format.format)),
          m_depth_format(vulkan_config.depth_format), m_msaa_samples(vulkan_config.msaa_samples),
          m_color_clear_value(render_config.clear_color) {
        LOG_INFO("create frame scheduler");
        m_frame_scheduler = std::make_unique<FrameScheduler>(
            context.get_device(), render_config.max_frames_in_flight);
    }

    void SceneRenderer::set_swapchain_resource_callbacks(
        SwapchainReleaseCallback release_resources, SwapchainRebuildCallback rebuild_resources) {
        m_release_swapchain_resources = std::move(release_resources);
        m_rebuild_swapchain_resources = std::move(rebuild_resources);
    }

    Result<void, GraphicsError> SceneRenderer::setup_render_pass() {
        LOG_INFO("create render pass");

        std::vector<Attachment> attachments;
        attachments.emplace_back(
            Attachment::get_color_attachment(m_surface_format, m_msaa_samples));
        attachments.emplace_back(Attachment::get_depth_attachment(m_depth_format, m_msaa_samples));

        std::vector<RenderSubPass> render_sub_passes;
        RenderSubPass render_sub_pass_0 = {
            {}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)}, m_msaa_samples};
        render_sub_passes.emplace_back(render_sub_pass_0);

        auto pass = RenderPass::create(
            m_context.get_device(), attachments, render_sub_passes, m_surface_format);
        if(!pass)
            return Result<void, GraphicsError>::failure(pass.error());
        auto target = RenderTarget::create_swapchain_target(
            m_context.get_device(), *pass.value(), m_context.get_swapchain());
        if(!target)
            return Result<void, GraphicsError>::failure(target.error());
        auto pipelines = std::make_unique<PipelineManager>(m_context.get_device(), *pass.value());
        reset_render_pipeline();
        m_render_pass = std::move(pass).value();
        m_pipeline_manager = std::move(pipelines);
        m_render_target = std::move(target).value();
        set_render_target_clear_color();

        const auto image_count =
            static_cast<uint32_t>(m_context.get_swapchain().get_images().size());
        m_frame_scheduler->initialize_swapchain_images(image_count);

        m_uses_offscreen_target = false;
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::setup_offscreen_render_pass(const Math::Vec2u size) {
        if(size.x == 0 || size.y == 0) {
            return Result<void, GraphicsError>::failure(
                {"Offscreen render target size must be greater than zero"});
        }

        LOG_INFO("create offscreen render pass at {}x{}", size.x, size.y);

        Attachment color_attachment =
            Attachment::get_color_attachment(m_surface_format, m_msaa_samples);
        if(m_msaa_samples == SampleCount::Count1) {
            color_attachment.description.store_op = AttachmentStoreOp::Store;
            color_attachment.description.final_layout = ImageLayout::ShaderReadOnlyOptimal;
            color_attachment.usage |= ImageUsage::Sampled;
        }

        std::vector<Attachment> attachments;
        attachments.emplace_back(color_attachment);
        attachments.emplace_back(Attachment::get_depth_attachment(m_depth_format, m_msaa_samples));

        RenderSubPass render_sub_pass = {
            {}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)}, m_msaa_samples};
        render_sub_pass.resolve_final_layout = ImageLayout::ShaderReadOnlyOptimal;
        render_sub_pass.resolve_usage =
            Flags<ImageUsage>(ImageUsage::ColorAttachment) | ImageUsage::Sampled;

        auto pass = RenderPass::create(m_context.get_device(), attachments,
            std::vector<RenderSubPass>{render_sub_pass}, m_surface_format);
        if(!pass)
            return Result<void, GraphicsError>::failure(pass.error());
        auto target = RenderTarget::try_create_multi_target(
            m_context.get_device(), *pass.value(), size, m_frame_scheduler->get_frame_slot_count());
        if(!target)
            return Result<void, GraphicsError>::failure(target.error());
        auto pipelines = std::make_unique<PipelineManager>(m_context.get_device(), *pass.value());
        reset_render_pipeline();
        m_render_pass = std::move(pass).value();
        m_pipeline_manager = std::move(pipelines);
        m_render_target = std::move(target).value();
        set_render_target_clear_color();

        m_uses_offscreen_target = true;
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::setup_pipeline(ResourceManager& resource_manager) {
        auto materials = MaterialRenderer::create(m_context.get_device(), *m_pipeline_manager,
            resource_manager, m_frame_scheduler->get_frame_slot_count(), m_msaa_samples,
            m_material_shaders ? &*m_material_shaders : nullptr);
        if(!materials)
            return Result<void, GraphicsError>::failure(materials.error());
        auto debug = DebugRenderer::create(m_context.get_device(), *m_pipeline_manager,
            resource_manager, m_frame_scheduler->get_frame_slot_count(), m_msaa_samples);
        if(!debug)
            return Result<void, GraphicsError>::failure(debug.error());
        m_material_renderer = std::move(materials).value();
        m_debug_renderer = std::move(debug).value();
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::reload_material_shaders(
        MaterialRenderer::ShaderCode shaders) {
        if(!m_material_renderer || !m_pipeline_manager)
            return Result<void, GraphicsError>::failure({"Scene pipelines are not initialized"});
        auto result =
            m_material_renderer->reload_shaders(*m_pipeline_manager, shaders, m_msaa_samples);
        if(result)
            m_material_shaders = std::move(shaders);
        return result;
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
            command_buffer.set_viewport(
                Graphics::get_viewport(static_cast<float>(size.x), static_cast<float>(size.y)));
            command_buffer.set_scissor(
                Graphics::get_scissor(static_cast<float>(size.x), static_cast<float>(size.y)));
            if(m_material_renderer) {
                resource_waits = m_material_renderer->render(
                    *m_frame_scheduler, *submission.view_project_matrix, submission.render_items);
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

        auto acquisition = swapchain.acquire_next_image(frame_slot.image_available_semaphore);
        if(!acquisition)
            throw std::runtime_error(acquisition.error().message);
        if(!acquisition.value()) {
            if(!recreate_swapchain())
                return false;
            acquisition = swapchain.acquire_next_image(frame_slot.image_available_semaphore);
            if(!acquisition)
                throw std::runtime_error(acquisition.error().message);
            if(!acquisition.value())
                return false;
        }

        m_frame_scheduler->begin_frame(*acquisition.value());
        auto& command_buffer = m_frame_scheduler->get_current_command_buffer();
        command_buffer.begin(Flags<CommandBuffer::Usage>(CommandBuffer::Usage::OneTimeSubmit));

        return true;
    }

    void SceneRenderer::end_frame(const std::span<const QueueSemaphoreSubmit> resource_waits) {
        PROFILE_SCOPE("SceneRenderer::end_frame");

        auto& device = m_context.get_device();
        auto& swapchain = m_context.get_swapchain();
        const uint32_t image_index = swapchain.get_current_index();
        auto& frame_slot = m_frame_scheduler->get_current_frame_slot();
        auto& image_state = m_frame_scheduler->get_swapchain_image_state(image_index);

        frame_slot.command_buffer.end();

        std::vector<QueueSemaphoreSubmit> waits;
        waits.reserve(1 + resource_waits.size());
        waits.emplace_back(QueueSemaphoreSubmit{frame_slot.image_available_semaphore,
            Flags<PipelineStage>(PipelineStage::ColorAttachmentOutput)});
        waits.insert(waits.end(), resource_waits.begin(), resource_waits.end());
        const QueueSemaphoreSubmit render_finished_signal{image_state.render_finished_semaphore,
            Flags<PipelineStage>(PipelineStage::AllCommands)};
        const auto submission =
            m_frame_scheduler->submit(waits, std::span(&render_finished_signal, 1));
        if(!submission)
            throw std::runtime_error("Cannot submit render frame: " + submission.error().message);

        auto& present_queue = device.get_present_queue(0);
        const auto result = present_queue.present(
            swapchain, std::span(&image_state.render_finished_semaphore, 1), image_index);
        if(!result)
            throw std::runtime_error(result.error().message);
        if(result.value() == Queue::PresentStatus::RecreateRequired)
            static_cast<void>(recreate_swapchain());

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
            if(candidate.error().is_device_lost())
                throw std::runtime_error(
                    "Device lost while resizing offscreen target: " + candidate.error().message);
            const Math::Vec2u current_size = m_render_target->get_size();
            LOG_ERROR("Keeping offscreen render target at {}x{} after {}x{} generation "
                      "creation failed: {}",
                current_size.x, current_size.y, size.x, size.y, candidate.error().message);
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
            LOG_FATAL("Offscreen frame slot {} exceeds frame slot count {}", frame_slot_index,
                m_frame_scheduler->get_frame_slot_count());
        }
        return m_render_target->get_color_view(frame_slot_index);
    }

    bool SceneRenderer::recreate_swapchain() {
        PROFILE_SCOPE("SceneRenderer::recreate_swapchain");
        auto& swapchain = m_context.get_swapchain();
        const SwapchainConfig previous_config = swapchain.get_active_generation()->get_config();

        m_frame_scheduler->wait_for_all_slots();
        m_context.get_device().get_present_queue(0).wait_idle();
        if(!m_uses_offscreen_target) {
            m_render_target.reset();
        }
        if(m_release_swapchain_resources) {
            m_release_swapchain_resources();
        }

        const auto recreation = swapchain.recreate();
        if(!recreation)
            throw std::runtime_error("Cannot rebuild presentation: " + recreation.error().message);
        const bool recreated = recreation.value() == Swapchain::RecreateStatus::Recreated;
        const SwapchainCompatibility compatibility = compare_swapchain_configs(
            previous_config, swapchain.get_active_generation()->get_config());
        if(!m_uses_offscreen_target && compatibility.format_changed) {
            throw std::runtime_error(
                "Runtime swapchain format changed; RenderPass/Pipeline generation "
                "rebuild is not implemented yet");
        }
        if(!m_uses_offscreen_target) {
            auto target = RenderTarget::create_swapchain_target(
                m_context.get_device(), *m_render_pass, swapchain);
            if(!target)
                throw std::runtime_error(
                    "Cannot rebuild swapchain render target: " + target.error().message);
            m_render_target = std::move(target).value();
            set_render_target_clear_color();
        }
        if(recreated) {
            const auto image_count = static_cast<uint32_t>(swapchain.get_images().size());
            m_frame_scheduler->initialize_swapchain_images(image_count);
        }
        if(m_rebuild_swapchain_resources) {
            auto result = m_rebuild_swapchain_resources(compatibility);
            if(!result)
                throw std::runtime_error(
                    "Cannot rebuild swapchain overlay: " + result.error().message);
        }
        return recreated;
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
