#include "render/scene/scene_renderer.h"
#include "render/render_graph.h"
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
#include "render/resource/resource_manager.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"

#include <utility>

namespace Comet {
    // 同一兼容性版本，按依赖的逆序析构；在途帧保留整代，resize 另保留实际目标。
    struct SceneRenderer::TargetState {
        std::shared_ptr<RenderPass> pass;
        std::unique_ptr<PipelineManager> pipelines;
        std::shared_ptr<RenderTarget> target;
        std::unique_ptr<MaterialRenderer> materials;
        std::unique_ptr<DebugRenderer> debug;
        std::optional<RenderGraph::Plan> graph;
        bool offscreen = false;
    };

    SceneRenderer::SceneRenderer(Device& device, Format surface_format,
        const Config::Vulkan& vulkan, const Config::Render& render)
        : m_device(device), m_surface_format(surface_format), m_depth_format(vulkan.depth_format),
          m_msaa_samples(vulkan.msaa_samples), m_clear_color(render.clear_color),
          m_frame_slot_count(render.max_frames_in_flight) {}

    Result<std::shared_ptr<SceneRenderer::TargetState>, GraphicsError> SceneRenderer::create_target(
        ResourceManager& resources, Swapchain* swapchain, Math::Vec2u size) {
        using Creation = Result<std::shared_ptr<TargetState>, GraphicsError>;
        if(!swapchain && (size.x == 0 || size.y == 0))
            return Creation::failure({"Offscreen render target size must be greater than zero"});
        auto next = std::make_shared<TargetState>();
        next->offscreen = !swapchain;
        auto color = Attachment::get_color_attachment(m_surface_format, m_msaa_samples);
        auto depth = Attachment::get_depth_attachment(m_depth_format, m_msaa_samples);
        if(next->offscreen) {
            color.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
            color.description.final_layout = ImageLayout::ColorAttachmentOptimal;
            depth.description.initial_layout = ImageLayout::DepthStencilAttachmentOptimal;
        }
        if(next->offscreen && m_msaa_samples == SampleCount::Count1) {
            color.description.store_op = AttachmentStoreOp::Store;
            color.usage |= ImageUsage::Sampled;
        }
        RenderSubPass subpass{
            {}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)}, m_msaa_samples};
        if(next->offscreen) {
            subpass.resolve_initial_layout = ImageLayout::ColorAttachmentOptimal;
            subpass.resolve_final_layout = ImageLayout::ColorAttachmentOptimal;
            subpass.resolve_usage =
                Flags<ImageUsage>(ImageUsage::ColorAttachment) | ImageUsage::Sampled;
        }
        auto pass = RenderPass::create(m_device,
            {color, depth}, {subpass},
            m_surface_format);
        if(!pass)
            return Creation::failure(pass.error());
        next->pass = std::move(pass).value();
        if(swapchain) {
            auto target = RenderTarget::create_swapchain_target(m_device, *next->pass, *swapchain);
            if(!target)
                return Creation::failure(target.error());
            next->target = std::move(target).value();
        } else {
            auto target = RenderTarget::try_create_multi_target(
                m_device, *next->pass, size, m_frame_slot_count);
            if(!target)
                return Creation::failure(target.error());
            next->target = std::move(target).value();
        }
        next->target->set_clear_value(ClearValue(m_clear_color));
        next->pipelines = std::make_unique<PipelineManager>(m_device, *next->pass);
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
        if(next->offscreen) {
            RenderGraph graph;
            RenderGraph::Pass scene{"scene", {}};
            RenderGraph::ResourceId output;
            for(const auto& attachment : next->pass->get_attachments()) {
                const bool is_depth =
                    Graphics::is_depth_stencil_format(attachment.description.format);
                auto aspects =
                    Flags<ImageAspect>(is_depth ? ImageAspect::Depth : ImageAspect::Color);
                if(is_depth && !Graphics::is_depth_only_format(attachment.description.format))
                    aspects |= ImageAspect::Stencil;
                const auto id =
                    graph.import_image("attachment " + std::to_string(scene.uses.size()),
                        {.subresources = {.aspects = aspects}});
                scene.uses.push_back({id,
                    is_depth ? ResourceUsage::DepthStencilAttachmentWrite
                             : ResourceUsage::ColorAttachmentWrite,
                    {}});
                if(!is_depth)
                    output = id;
            }
            graph.add_pass(std::move(scene));
            graph.export_resource({output, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)});
            auto compiled = graph.compile();
            if(!compiled)
                return Creation::failure({compiled.error()});
            next->graph = std::move(compiled).value();
        }
        return Creation::success(std::move(next));
    }

    Result<void, GraphicsError> SceneRenderer::configure_presentation(
        ResourceManager& resources, Swapchain& swapchain) {
        auto next = create_target(resources, &swapchain, {});
        if(!next)
            return Result<void, GraphicsError>::failure(next.error());
        m_target = std::move(next).value();
        m_resize_failure.reset();
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::configure_offscreen(
        ResourceManager& resources, Math::Vec2u size) {
        auto next = create_target(resources, nullptr, size);
        if(!next)
            return Result<void, GraphicsError>::failure(next.error());
        m_target = std::move(next).value();
        m_resize_failure.reset();
        return Result<void, GraphicsError>::success();
    }

    Result<MaterialRenderer::ReloadReport, GraphicsError> SceneRenderer::reload_material_shaders(
        MaterialRenderer::ShaderCode shaders) {
        if(!m_target)
            return Result<MaterialRenderer::ReloadReport, GraphicsError>::failure(
                {"Scene pipelines are not initialized"});
        auto result =
            m_target->materials->reload_shaders(*m_target->pipelines, shaders, m_msaa_samples);
        if(result)
            m_material_shaders = std::move(shaders);
        return result;
    }

    std::vector<std::shared_ptr<const MaterialLayout>> SceneRenderer::get_material_layouts() const {
        if(!m_target)
            return {};
        return m_target->materials->get_material_layouts();
    }

    const MaterialRenderer::Statistics& SceneRenderer::get_material_statistics() const {
        return m_target->materials->get_statistics();
    }

    RenderTarget& SceneRenderer::get_render_target() {
        return *m_target->target;
    }
    const RenderTarget& SceneRenderer::get_render_target() const {
        return *m_target->target;
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> SceneRenderer::render_scene_pass(
        FrameScheduler& frames, const RenderSubmission& submission, const LineDrawList& lines) {
        PROFILE_SCOPE("SceneRenderer::render_scene_pass");
        frames.retain_current_frame_resource(m_target);
        frames.retain_current_frame_resource(m_target->target);
        if(!m_target->graph)
            return draw_scene(frames, frames.get_current_command_buffer(), submission, lines);

        const auto image = frames.get_current_frame_slot_index();
        std::vector<RenderGraph::Binding> bindings;
        for(const auto& view : m_target->target->get_framebuffer(image)->get_attachments())
            bindings.emplace_back(view->get_image());
        std::vector<QueueSemaphoreSubmit> waits;
        const auto recorded = m_target->graph->record(frames, bindings,
            [this, &frames, &submission, &lines, &waits](size_t, CommandBuffer& command) {
                auto drawn = draw_scene(frames, command, submission, lines);
                if(!drawn)
                    return Result<void, GraphicsError>::failure(drawn.error());
                waits = std::move(drawn).value();
                return Result<void, GraphicsError>::success();
            });
        if(!recorded)
            return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::failure(
                recorded.error());
        return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::success(std::move(waits));
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> SceneRenderer::draw_scene(
        FrameScheduler& frames, CommandBuffer& command, const RenderSubmission& submission,
        const LineDrawList& lines) {
        if(m_target->offscreen)
            m_target->target->begin_render_target(command, frames.get_current_frame_slot_index());
        else
            m_target->target->begin_render_target(command);
        const auto size = m_target->target->get_size();
        command.set_viewport(
            Graphics::get_viewport(static_cast<float>(size.x), static_cast<float>(size.y)));
        command.set_scissor(
            Graphics::get_scissor(static_cast<float>(size.x), static_cast<float>(size.y)));
        auto waits = m_target->materials->render(
            frames, submission.view_project_matrix, submission.render_items);
        if(!waits)
            return waits;
        if(submission.view_project_matrix) {
            if(auto debug = m_target->debug->render(frames, *submission.view_project_matrix, lines);
                !debug)
                return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::failure(
                    debug.error());
        }
        m_target->target->end_render_target(command);
        return waits;
    }

    Result<void, GraphicsError> SceneRenderer::resize_offscreen_target(
        const Math::Vec2u size, const std::chrono::steady_clock::time_point now) {
        if(!m_target->offscreen || size.x == 0 || size.y == 0) {
            return Result<void, GraphicsError>::success();
        }
        if(m_resize_failure && m_resize_failure->size != size)
            m_resize_failure.reset();
        if(m_target->target->get_size() == size) {
            m_resize_failure.reset();
            return Result<void, GraphicsError>::success();
        }
        if(m_resize_failure && !m_resize_failure->retry.consume(now))
            return Result<void, GraphicsError>::success();

        auto candidate = RenderTarget::try_create_multi_target(
            m_device, *m_target->pass, size, m_frame_slot_count);
        if(!candidate) {
            if(candidate.error().is_device_lost())
                return Result<void, GraphicsError>::failure(candidate.error());
            const Math::Vec2u current_size = m_target->target->get_size();
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

        std::shared_ptr<RenderTarget> next_generation(std::move(candidate).value());
        next_generation->set_clear_value(ClearValue(m_clear_color));
        LOG_INFO("Commit offscreen render target generation {}x{}", size.x, size.y);
        m_target->target = std::move(next_generation);
        m_resize_failure.reset();
        return Result<void, GraphicsError>::success();
    }

    std::shared_ptr<ImageView> SceneRenderer::get_offscreen_color_view(uint32_t slot) const {
        if(!m_target || !m_target->offscreen)
            return nullptr;
        if(slot >= m_frame_slot_count)
            LOG_FATAL(
                "Offscreen frame slot {} exceeds frame slot count {}", slot, m_frame_slot_count);
        return m_target->target->get_color_view(slot);
    }

    void SceneRenderer::release_presentation_target() {
        if(m_target && !m_target->offscreen)
            m_target->target.reset();
    }

    Result<void, GraphicsError> SceneRenderer::rebuild_presentation_target(
        Swapchain& swapchain, const SwapchainCompatibility& compatibility) {
        if(m_target->offscreen)
            return Result<void, GraphicsError>::success();
        if(compatibility.format_changed)
            return Result<void, GraphicsError>::failure(
                {"Runtime swapchain format changed; RenderPass/Pipeline generation rebuild is not implemented yet"});
        auto target = RenderTarget::create_swapchain_target(m_device, *m_target->pass, swapchain);
        if(!target)
            return Result<void, GraphicsError>::failure(target.error());
        target.value()->set_clear_value(ClearValue(m_clear_color));
        m_target->target = std::move(target).value();
        return Result<void, GraphicsError>::success();
    }
}
