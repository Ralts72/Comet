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
#include "graphics/resource/image_view.h"
#include "graphics/frame_buffer.h"
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
        const auto extent =
            m_context.get_swapchain().get_active_generation()->get_config().extent;
        setup_targets({extent.width, extent.height}, false);
        const auto image_count =
            static_cast<uint32_t>(m_context.get_swapchain().get_images().size());
        m_frame_scheduler->initialize_swapchain_images(image_count);
    }

    void SceneRenderer::setup_offscreen_render_pass(const Math::Vec2u size) {
        setup_targets(size, true);
    }

    void SceneRenderer::setup_targets(const Math::Vec2u size, const bool offscreen) {
        if(size.x == 0 || size.y == 0) {
            LOG_FATAL("Render target size must be greater than zero");
        }

        LOG_INFO("Create HDR scene and SDR output passes at {}x{}", size.x, size.y);
        reset_render_pipeline();
        m_uses_offscreen_target = offscreen;
        constexpr auto hdr_format = Format::R16G16B16A16_SFLOAT;

        Attachment color_attachment =
            Attachment::get_color_attachment(hdr_format, m_msaa_samples);
        color_attachment.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
        color_attachment.description.final_layout = ImageLayout::ColorAttachmentOptimal;
        if(m_msaa_samples == SampleCount::Count1) {
            color_attachment.description.store_op = AttachmentStoreOp::Store;
            color_attachment.usage |= ImageUsage::Sampled;
        }

        std::vector<Attachment> attachments;
        attachments.emplace_back(color_attachment);
        attachments.emplace_back(
            Attachment::get_depth_attachment(m_depth_format, m_msaa_samples));
        attachments.back().description.initial_layout =
            ImageLayout::DepthStencilAttachmentOptimal;

        RenderSubPass render_sub_pass = {{}, {SubpassColorAttachment(0)},
            {SubpassDepthStencilAttachment(1)}, m_msaa_samples};
        render_sub_pass.resolve_initial_layout = ImageLayout::ColorAttachmentOptimal;
        render_sub_pass.resolve_final_layout = ImageLayout::ColorAttachmentOptimal;
        render_sub_pass.resolve_usage =
            Flags<ImageUsage>(ImageUsage::ColorAttachment) | ImageUsage::Sampled;

        m_render_pass = std::make_shared<RenderPass>(m_context.get_device(), attachments,
            std::vector<RenderSubPass>{render_sub_pass}, hdr_format);
        m_pipeline_manager =
            std::make_unique<PipelineManager>(m_context.get_device(), *m_render_pass);
        m_scene_target = RenderTarget::create_multi_target(m_context.get_device(),
            *m_render_pass, size, m_frame_scheduler->get_frame_slot_count());
        m_scene_target->set_clear_value(m_color_clear_value);
        // 与窗口编码一致：sRGB 采样解码/附件编码，UNORM 则传递已编码的 SDR 值。
        m_post_processor = std::make_unique<PostProcessRenderer>(m_context.get_device(),
            m_surface_format, offscreen, m_frame_scheduler->get_frame_slot_count());
        if(offscreen)
            m_render_target = RenderTarget::create_multi_target(m_context.get_device(),
                m_post_processor->get_render_pass(), size,
                m_frame_scheduler->get_frame_slot_count());
        else
            m_render_target =
                RenderTarget::create_swapchain_target(m_context.get_device(),
                    m_post_processor->get_render_pass(), m_context.get_swapchain());

        RenderGraph graph;
        RenderGraph::Pass scene_pass{"scene", {}};
        RenderGraph::Pass post_pass{"tone map", {}};
        for(const auto& attachment : m_render_pass->get_attachments()) {
            const bool depth =
                Graphics::is_depth_stencil_format(attachment.description.format);
            auto aspects =
                Flags<ImageAspect>(depth ? ImageAspect::Depth : ImageAspect::Color);
            if(depth && !Graphics::is_depth_only_format(attachment.description.format))
                aspects |= ImageAspect::Stencil;
            const auto id = graph.import_image(
                "scene attachment " + std::to_string(scene_pass.uses.size()),
                *resolve_image_state(ResourceUsage::Undefined, {.aspects = aspects}));
            scene_pass.uses.push_back({id,
                depth ? ResourceUsage::DepthStencilAttachmentWrite
                      : ResourceUsage::ColorAttachmentWrite,
                {}});
            if(static_cast<bool>(attachment.usage & ImageUsage::Sampled))
                post_pass.uses.push_back({id, ResourceUsage::SampledRead,
                    Flags<PipelineStage>(PipelineStage::FragmentShader)});
        }
        graph.add_pass(std::move(scene_pass));
        graph.add_pass(std::move(post_pass));
        m_render_plan = graph.compile();
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
        const auto frame_buffer = m_scene_target->get_framebuffer(
            m_frame_scheduler->get_current_frame_slot_index());
        std::vector<RenderGraph::Binding> bindings;
        for(const auto& view : frame_buffer->get_attachments())
            bindings.emplace_back(view->get_image());
        std::vector<QueueSemaphoreSubmit> waits;
        m_render_plan->record(
            *m_frame_scheduler, bindings, [&](size_t pass, const CommandBuffer&) {
                if(pass == 0) {
                    waits = record_scene_pass(submission, lines);
                } else {
                    const auto slot = m_frame_scheduler->get_current_frame_slot_index();
                    const auto index =
                        m_uses_offscreen_target
                            ? slot
                            : m_context.get_swapchain().get_current_index();
                    m_post_processor->render(*m_frame_scheduler, m_render_target, index,
                        m_scene_target->get_color_view(slot));
                }
            });
        return waits;
    }

    std::vector<QueueSemaphoreSubmit> SceneRenderer::record_scene_pass(
        const RenderSubmission& submission, const LineDrawList& lines) {

        auto& command_buffer = m_frame_scheduler->get_current_command_buffer();
        m_frame_scheduler->retain_current_frame_resource(m_scene_target);
        m_frame_scheduler->retain_current_frame_resource(m_render_pass);
        m_scene_target->begin_render_target(
            command_buffer, m_frame_scheduler->get_current_frame_slot_index());

        std::vector<QueueSemaphoreSubmit> resource_waits;
        if(submission.view_project_matrix) {
            const auto size = m_scene_target->get_size();
            command_buffer.set_viewport(Graphics::get_viewport(
                static_cast<float>(size.x), static_cast<float>(size.y)));
            command_buffer.set_scissor(Graphics::get_scissor(
                static_cast<float>(size.x), static_cast<float>(size.y)));
            if(m_material_renderer) {
                resource_waits = m_material_renderer->render(*m_frame_scheduler,
                    *submission.view_project_matrix, submission.render_items,
                    submission.lights);
            }
            if(m_debug_renderer) {
                m_debug_renderer->render(
                    *m_frame_scheduler, *submission.view_project_matrix, lines);
            }
        }

        m_scene_target->end_render_target(command_buffer);
        return resource_waits;
    }

    bool SceneRenderer::begin_frame() {
        PROFILE_SCOPE("SceneRenderer::begin_frame");
        if(m_swapchain_rebuild_from) {
            if(std::chrono::steady_clock::now() < m_swapchain_retry_after
                || !recreate_swapchain()) {
                return false;
            }
        }
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
                // 窗口可能在重建与第二次 acquire 之间再次变化；下一帧重新尝试。
                return false;
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

        static_cast<void>(resize_targets(size));
    }

    bool SceneRenderer::resize_targets(const Math::Vec2u size) {
        auto candidate = RenderTarget::try_create_multi_target(m_context.get_device(),
            *m_render_pass, size, m_frame_scheduler->get_frame_slot_count());
        if(!candidate) {
            const Math::Vec2u current_size = m_scene_target->get_size();
            LOG_ERROR("Keeping offscreen render target at {}x{} after {}x{} generation "
                      "creation failed: {}",
                current_size.x, current_size.y, size.x, size.y,
                vk::to_string(candidate.result()));
            return false;
        }

        std::shared_ptr<RenderTarget> next_generation(std::move(candidate).value());
        next_generation->set_clear_value(m_color_clear_value);
        std::shared_ptr<RenderTarget> output;
        if(m_uses_offscreen_target) {
            auto attempt = RenderTarget::try_create_multi_target(m_context.get_device(),
                m_post_processor->get_render_pass(), size,
                m_frame_scheduler->get_frame_slot_count());
            if(!attempt) {
                LOG_ERROR(
                    "Keeping HDR/output targets after SDR target creation failed: {}",
                    vk::to_string(attempt.result()));
                return false;
            }
            output = std::move(attempt).value();
        } else {
            output = RenderTarget::create_swapchain_target(m_context.get_device(),
                m_post_processor->get_render_pass(), m_context.get_swapchain());
        }
        LOG_INFO("Commit HDR/output render target generation {}x{}", size.x, size.y);
        m_scene_target = std::move(next_generation);
        m_render_target = std::move(output);
        return true;
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
        if(!m_swapchain_rebuild_from) {
            m_swapchain_rebuild_from = swapchain.get_active_generation()->get_config();
            m_frame_scheduler->wait_for_all_slots();
            m_context.get_device().get_present_queue(0).wait_idle();
            if(!m_uses_offscreen_target) {
                m_render_target.reset();
            }
            if(m_release_swapchain_resources) {
                m_release_swapchain_resources();
            }
        }

        if(!swapchain.recreate()) {
            m_swapchain_retry_after =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            return false;
        }

        const SwapchainCompatibility compatibility = compare_swapchain_configs(
            *m_swapchain_rebuild_from, swapchain.get_active_generation()->get_config());
        if(!m_uses_offscreen_target && compatibility.format_changed) {
            LOG_FATAL("Runtime swapchain format changed; RenderPass/Pipeline generation "
                      "rebuild is not implemented yet");
        }
        if(!m_uses_offscreen_target) {
            const auto extent = swapchain.get_active_generation()->get_config().extent;
            if(!resize_targets({extent.width, extent.height})) {
                m_swapchain_retry_after =
                    std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
                return false;
            }
        }

        const auto image_count = static_cast<uint32_t>(swapchain.get_images().size());
        m_frame_scheduler->initialize_swapchain_images(image_count);

        if(m_rebuild_swapchain_resources) {
            m_rebuild_swapchain_resources(compatibility);
        }
        m_swapchain_rebuild_from.reset();
        return true;
    }

    void SceneRenderer::reset_render_pipeline() {
        m_render_plan.reset();
        m_debug_renderer.reset();
        m_material_renderer.reset();
        m_pipeline_manager.reset();
        m_render_target.reset();
        m_scene_target.reset();
        m_post_processor.reset();
        m_render_pass.reset();
    }

}
