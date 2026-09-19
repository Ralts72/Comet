#include "render/scene/scene_renderer.h"
#include "render/material/material_layout.h"
#include "render/render_graph.h"
#include "render/passes/output_pass.h"
#include "render/passes/shadow_pass.h"
#include "graphics/frame_buffer.h"
#include "graphics/resource/image_view.h"
#include "graphics/device.h"
#include "graphics/render_pass.h"
#include "graphics/attachment.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/convert.h"
#include "render/render_target.h"
#include "render/frame_scheduler.h"
#include "render/debug/debug_renderer.h"
#include "render/resource/render_resources.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"

#include <utility>

namespace Comet {
    // 同一兼容性版本，按依赖的逆序析构；在途帧保留整代，resize 另保留实际目标。
    struct SceneRenderer::RenderState {
        std::shared_ptr<RenderPass> scene_pass;
        std::unique_ptr<PipelineManager> pipelines;
        std::unique_ptr<OutputPass> output_pass;
        std::unique_ptr<ShadowPass> shadow_pass;
        std::shared_ptr<RenderTarget> hdr_target;
        std::shared_ptr<RenderTarget> output_target;
        std::unique_ptr<MaterialRenderer> materials;
        std::unique_ptr<DebugRenderer> debug;
        RenderGraph::Plan graph;
        RenderGraph::PassId shadow_pass_id;
        RenderGraph::PassId scene_pass_id;
        RenderGraph::PassId output_pass_id;
        bool offscreen = false;
    };

    SceneRenderer::SceneRenderer(
        Device& device, const Config::Vulkan& vulkan, const Config::Render& render)
        : m_device(device), m_offscreen_format(vulkan.surface_format),
          m_hdr_headroom(render.hdr_headroom), m_depth_format(vulkan.depth_format),
          m_msaa_samples(vulkan.msaa_samples), m_clear_color(render.clear_color),
          m_frame_slot_count(render.max_frames_in_flight) {}

    Result<std::shared_ptr<SceneRenderer::RenderState>, GraphicsError> SceneRenderer::create_state(
        RenderResources& resources, Swapchain* swapchain, Math::Vec2u size) {
        using Creation = Result<std::shared_ptr<RenderState>, GraphicsError>;
        if(!swapchain && (size.x == 0 || size.y == 0))
            return Creation::failure({"Offscreen render target size must be greater than zero"});
        if(auto supported = validate_color_target(m_device.get_capability().physical_device,
               Config::Render::SCENE_COLOR_FORMAT, m_msaa_samples);
            !supported)
            return Creation::failure(supported.error());
        auto next = std::make_shared<RenderState>();
        next->offscreen = !swapchain;
        auto color =
            Attachment::get_color_attachment(Config::Render::SCENE_COLOR_FORMAT, m_msaa_samples);
        auto depth = Attachment::get_depth_attachment(m_depth_format, m_msaa_samples);
        color.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
        color.description.final_layout = ImageLayout::ColorAttachmentOptimal;
        depth.description.initial_layout = ImageLayout::DepthStencilAttachmentOptimal;
        if(m_msaa_samples == SampleCount::Count1) {
            color.description.store_op = AttachmentStoreOp::Store;
            color.usage |= ImageUsage::Sampled;
        }
        RenderSubPass subpass{.color_attachments = {SubpassColorAttachment(0)},
            .depth_stencil_attachments = {SubpassDepthStencilAttachment(1)},
            .sample_count = m_msaa_samples};
        subpass.resolve_initial_layout = ImageLayout::ColorAttachmentOptimal;
        subpass.resolve_final_layout = ImageLayout::ColorAttachmentOptimal;
        subpass.resolve_usage =
            Flags<ImageUsage>(ImageUsage::ColorAttachment) | ImageUsage::Sampled;
        auto pass = RenderPass::create(
            m_device, {color, depth}, {subpass}, Config::Render::SCENE_COLOR_FORMAT);
        if(!pass)
            return Creation::failure(pass.error());
        next->scene_pass = std::move(pass).value();
        vk::SurfaceFormatKHR surface_output;
        if(swapchain)
            surface_output = swapchain->get_active_generation()->get_config().surface_format;
        else
            surface_output = vk::SurfaceFormatKHR{
                Graphics::format_to_vk(m_offscreen_format), vk::ColorSpaceKHR::eSrgbNonlinear};
        auto output_pass = OutputPass::create(m_device,
            Graphics::vk_to_format(surface_output.format), next->offscreen, m_frame_slot_count,
            Graphics::vk_to_image_color_space(surface_output.colorSpace), m_hdr_headroom);
        if(!output_pass)
            return Creation::failure(output_pass.error());
        next->output_pass = std::move(output_pass).value();
        auto shadow_pass = ShadowPass::create(m_device, m_frame_slot_count);
        if(!shadow_pass)
            return Creation::failure(shadow_pass.error());
        next->shadow_pass = std::move(shadow_pass).value();
        if(auto targets = replace_targets(*next, swapchain, size); !targets)
            return Creation::failure(targets.error());
        next->pipelines = std::make_unique<PipelineManager>(m_device, *next->scene_pass);
        auto materials =
            MaterialRenderer::create(m_device, *next->pipelines, resources, m_frame_slot_count,
                m_msaa_samples, m_material_shaders ? &*m_material_shaders : nullptr);
        if(!materials)
            return Creation::failure(materials.error());
        next->materials = std::move(materials).value();
        auto debug =
            DebugRenderer::create(m_device, *next->pipelines, m_frame_slot_count, m_msaa_samples);
        if(!debug)
            return Creation::failure(debug.error());
        next->debug = std::move(debug).value();
        RenderGraph graph;
        const auto shadow = graph.import_image(
            "shadow depth", {.subresources = {.aspects = Flags<ImageAspect>(ImageAspect::Depth)}});
        next->shadow_pass_id = graph.add_pass(
            {"directional shadow", {{shadow, ResourceUsage::DepthStencilAttachmentWrite, {}}}});
        RenderGraph::Pass scene{"scene", {}};
        RenderGraph::ResourceId output;
        for(const auto& attachment : next->scene_pass->get_attachments()) {
            const bool is_depth = Graphics::is_depth_stencil_format(attachment.description.format);
            auto aspects = Flags<ImageAspect>(is_depth ? ImageAspect::Depth : ImageAspect::Color);
            if(is_depth && !Graphics::is_depth_only_format(attachment.description.format))
                aspects |= ImageAspect::Stencil;
            const auto id = graph.import_image("attachment " + std::to_string(scene.uses.size()),
                {.subresources = {.aspects = aspects}});
            auto usage = ResourceUsage::ColorAttachmentWrite;
            if(is_depth)
                usage = ResourceUsage::DepthStencilAttachmentWrite;
            scene.uses.push_back({id, usage, {}});
            if(!is_depth)
                output = id;
        }
        scene.uses.push_back({shadow, ResourceUsage::SampledRead,
            Flags<PipelineStage>(PipelineStage::FragmentShader)});
        next->scene_pass_id = graph.add_pass(std::move(scene));
        next->output_pass_id =
            graph.add_pass({"tone map", {{output, ResourceUsage::SampledRead,
                                            Flags<PipelineStage>(PipelineStage::FragmentShader)}}});
        auto compiled = graph.compile();
        if(!compiled)
            return Creation::failure({compiled.error()});
        next->graph = std::move(compiled).value();
        return Creation::success(std::move(next));
    }

    Result<void, GraphicsError> SceneRenderer::replace_targets(
        RenderState& state, Swapchain* swapchain, Math::Vec2u size) {
        if(swapchain) {
            const auto generation = swapchain->get_active_generation();
            if(!generation)
                return Result<void, GraphicsError>::failure({"No active presentation generation"});
            const auto extent = generation->get_config().extent;
            size = {extent.width, extent.height};
        }
        auto hdr = RenderTarget::try_create_multi_target(
            m_device, *state.scene_pass, size, m_frame_slot_count);
        if(!hdr)
            return Result<void, GraphicsError>::failure(hdr.error());
        std::shared_ptr<RenderTarget> output;
        if(swapchain) {
            auto candidate = RenderTarget::create_swapchain_target(
                m_device, state.output_pass->get_render_pass(), *swapchain);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            output = std::move(candidate).value();
        } else {
            auto candidate = RenderTarget::try_create_multi_target(
                m_device, state.output_pass->get_render_pass(), size, m_frame_slot_count);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            output = std::move(candidate).value();
        }
        hdr.value()->set_clear_value(ClearValue(m_clear_color));
        // 两套候选均成功后再发布，不改变在途帧保留的旧目标。
        state.hdr_target = std::move(hdr).value();
        state.output_target = std::move(output);
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::configure_presentation(
        RenderResources& resources, Swapchain& swapchain) {
        auto next = create_state(resources, &swapchain, {});
        if(!next)
            return Result<void, GraphicsError>::failure(next.error());
        m_state = std::move(next).value();
        m_resize_failure.reset();
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::configure_offscreen(
        RenderResources& resources, Math::Vec2u size) {
        auto next = create_state(resources, nullptr, size);
        if(!next)
            return Result<void, GraphicsError>::failure(next.error());
        m_state = std::move(next).value();
        m_resize_failure.reset();
        return Result<void, GraphicsError>::success();
    }

    Result<MaterialRenderer::ReloadReport, GraphicsError> SceneRenderer::reload_material_shaders(
        MaterialShaders shaders) {
        if(!m_state)
            return Result<MaterialRenderer::ReloadReport, GraphicsError>::failure(
                {"Scene pipelines are not initialized"});
        auto result =
            m_state->materials->reload_shaders(*m_state->pipelines, shaders, m_msaa_samples);
        if(result) {
            if(!m_material_shaders)
                m_material_shaders.emplace();
            merge_material_shaders(*m_material_shaders, std::move(shaders));
        }
        return result;
    }

    Result<MaterialRenderer::MaterialUpdate, GraphicsError> SceneRenderer::prepare_material_update(
        const AssetHandle handle, const std::shared_ptr<const Material>& material) {
        if(!m_state)
            return Result<MaterialRenderer::MaterialUpdate, GraphicsError>::failure(
                {"Scene renderer is not configured"});
        return m_state->materials->prepare_material_update(handle, material);
    }

    std::vector<std::shared_ptr<const MaterialLayout>> SceneRenderer::get_material_layouts() const {
        if(!m_state)
            return {};
        return m_state->materials->get_material_layouts();
    }

    const MaterialRenderer::Statistics& SceneRenderer::get_material_statistics() const {
        return m_state->materials->get_statistics();
    }

    RenderTarget& SceneRenderer::get_render_target() {
        return *m_state->output_target;
    }
    const RenderTarget& SceneRenderer::get_render_target() const {
        return *m_state->output_target;
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> SceneRenderer::render(
        FrameScheduler& frames, const RenderSubmission& submission, const LineDrawList& lines) {
        PROFILE_SCOPE("SceneRenderer::render");
        using RenderResult = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>;
        frames.retain_current_frame_resource(m_state);
        frames.retain_current_frame_resource(m_state->output_target);
        frames.retain_current_frame_resource(m_state->hdr_target);
        const auto image = frames.get_current_frame_slot_index();
        const auto lighting = ShadowPass::prepare(submission);
        std::vector<RenderGraph::Binding> bindings{
            m_state->shadow_pass->get_depth_view(image)->get_image()};
        for(const auto& view : m_state->hdr_target->get_framebuffer(image)->get_attachments())
            bindings.emplace_back(view->get_image());
        std::vector<QueueSemaphoreSubmit> waits;
        const auto recorded = m_state->graph.record(frames, bindings,
            [this, &frames, &submission, &lines, &waits, &lighting](
                RenderGraph::PassId pass, CommandBuffer& command) {
                if(pass == m_state->output_pass_id)
                    return m_state->output_pass->render(frames, m_state->output_target,
                        m_state->hdr_target->get_color_view(frames.get_current_frame_slot_index()));
                auto drawn = RenderResult::success({});
                if(pass == m_state->shadow_pass_id)
                    drawn = m_state->shadow_pass->render(frames, lighting, submission.render_items);
                else if(pass == m_state->scene_pass_id)
                    drawn = draw_scene(frames, command, submission, lines, lighting);
                else
                    return Result<void, GraphicsError>::failure({"Unknown scene render pass"});
                if(!drawn)
                    return Result<void, GraphicsError>::failure(drawn.error());
                for(const auto& wait : drawn.value())
                    merge_semaphore_wait(waits, wait);
                return Result<void, GraphicsError>::success();
            });
        if(!recorded)
            return RenderResult::failure(recorded.error());
        return RenderResult::success(std::move(waits));
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> SceneRenderer::draw_scene(
        FrameScheduler& frames, CommandBuffer& command, const RenderSubmission& submission,
        const LineDrawList& lines, const LightingData& lighting) {
        m_state->hdr_target->begin_render_target(command, frames.get_current_frame_slot_index());
        const auto size = m_state->hdr_target->get_size();
        command.set_viewport(
            Graphics::get_viewport(static_cast<float>(size.x), static_cast<float>(size.y)));
        command.set_scissor(
            Graphics::get_scissor(static_cast<float>(size.x), static_cast<float>(size.y)));
        auto waits = m_state->materials->render(frames, submission.view_project_matrix,
            submission.render_items, lighting,
            m_state->shadow_pass->get_depth_view(frames.get_current_frame_slot_index()));
        if(!waits)
            return waits;
        if(submission.view_project_matrix) {
            if(auto debug = m_state->debug->render(frames, *submission.view_project_matrix, lines);
                !debug)
                return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::failure(
                    debug.error());
        }
        m_state->hdr_target->end_render_target(command);
        return waits;
    }

    Result<void, GraphicsError> SceneRenderer::resize_offscreen_target(
        const Math::Vec2u size, const std::chrono::steady_clock::time_point now) {
        if(!m_state->offscreen || size.x == 0 || size.y == 0) {
            return Result<void, GraphicsError>::success();
        }
        if(m_resize_failure && m_resize_failure->size != size)
            m_resize_failure.reset();
        if(m_state->output_target->get_size() == size) {
            m_resize_failure.reset();
            return Result<void, GraphicsError>::success();
        }
        if(m_resize_failure && !m_resize_failure->retry.consume(now))
            return Result<void, GraphicsError>::success();

        auto candidate = replace_targets(*m_state, nullptr, size);
        if(!candidate) {
            if(candidate.error().is_device_lost())
                return Result<void, GraphicsError>::failure(candidate.error());
            const Math::Vec2u current_size = m_state->output_target->get_size();
            if(!m_resize_failure)
                m_resize_failure = ResizeFailure{size};
            auto& failure = *m_resize_failure;
            const auto attempts = failure.retry.retry_count() + 1;
            if(!candidate.error().is_out_of_memory() || !failure.retry.schedule(now)) {
                LOG_ERROR("Keeping offscreen target at {}x{}; resize to {}x{} stopped after {} "
                          "attempts, waiting for a new size: {}",
                    current_size.x, current_size.y, size.x, size.y, attempts,
                    candidate.error().message);
            } else if(attempts == 1) {
                LOG_WARN("Keeping offscreen target at {}x{}; resize to {}x{} will retry: {}",
                    current_size.x, current_size.y, size.x, size.y, candidate.error().message);
            }
            return Result<void, GraphicsError>::success();
        }

        LOG_INFO("Commit HDR and SDR target generation {}x{}", size.x, size.y);
        m_resize_failure.reset();
        return Result<void, GraphicsError>::success();
    }

    std::shared_ptr<ImageView> SceneRenderer::get_offscreen_color_view(uint32_t slot) const {
        if(!m_state || !m_state->offscreen)
            return nullptr;
        if(slot >= m_frame_slot_count)
            LOG_FATAL(
                "Offscreen frame slot {} exceeds frame slot count {}", slot, m_frame_slot_count);
        return m_state->output_target->get_color_view(slot);
    }

    void SceneRenderer::release_presentation_target() {
        if(m_state && !m_state->offscreen)
            m_state->output_target.reset();
    }

    Result<void, GraphicsError> SceneRenderer::rebuild_presentation_target(
        Swapchain& swapchain, const SwapchainCompatibility& compatibility) {
        if(m_state->offscreen)
            return Result<void, GraphicsError>::success();
        if(compatibility.format_changed)
            return Result<void, GraphicsError>::failure(
                {"Runtime swapchain format changed; RenderPass/Pipeline generation rebuild is not implemented yet"});
        return replace_targets(*m_state, &swapchain, {});
    }
}
