#include "render/scene/scene_renderer.h"
#include "render/material/material_layout.h"
#include "render/render_graph.h"
#include "render/render_diagnostics.h"
#include "render/passes/output_pass.h"
#include "render/passes/bloom_pass.h"
#include "render/passes/shadow_pass.h"
#include "render/passes/skybox_pass.h"
#include "render/resource/texture.h"
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
    // 配套绘制资源按依赖逆序析构，scene_pass 最后释放。
    // 在途帧保留整组资源；缩放会替换内部目标，因此还需单独保留本帧使用的目标。
    struct SceneRenderer::RenderState {
        std::shared_ptr<RenderPass> scene_pass;
        std::unique_ptr<PipelineManager> pipelines;
        std::unique_ptr<OutputPass> output_pass;
        std::unique_ptr<BloomPass> bloom;
        std::unique_ptr<ShadowPass> shadow_pass;
        std::unique_ptr<SkyboxPass> skybox_pass;
        std::shared_ptr<RenderTarget> hdr_target;
        std::shared_ptr<RenderTarget> output_target;
        std::unique_ptr<MaterialRenderer> materials;
        std::unique_ptr<DebugRenderer> debug;
        RenderGraph::Plan graph;
        RenderGraph::ResourceId shadow_resource;
        std::vector<RenderGraph::ResourceId> scene_attachments;
        RenderGraph::PassId shadow_pass_id;
        RenderGraph::PassId scene_pass_id;
        RenderGraph::PassId output_pass_id;
        std::optional<BloomPass::Passes> bloom_passes;
        bool offscreen = false;
    };

    SceneRenderer::SceneRenderer(Device& device, const AssetRegistry& assets,
        const Config::Vulkan& vulkan, const Config::Render& render)
        : m_device(device), m_assets(assets), m_offscreen_format(vulkan.surface_format),
          m_hdr_headroom(render.hdr_headroom), m_depth_format(vulkan.depth_format),
          m_msaa_samples(vulkan.msaa_samples), m_frame_slot_count(render.max_frames_in_flight) {}

    Result<std::shared_ptr<SceneRenderer::RenderState>, GraphicsError> SceneRenderer::create_state(
        RenderResources& resources, Swapchain* swapchain, Math::Vec2u size) {
        using Creation = Result<std::shared_ptr<RenderState>, GraphicsError>;
        if(auto valid = m_post_process.validate(); !valid)
            return Creation::failure({valid.error()});
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
        if(m_post_process.uses_bloom()) {
            auto bloom = BloomPass::create(m_device, m_frame_slot_count);
            if(!bloom)
                return Creation::failure(bloom.error());
            next->bloom = std::move(bloom).value();
        }
        if(auto targets = replace_targets(*next, swapchain, size); !targets)
            return Creation::failure(targets.error());
        next->pipelines = std::make_unique<PipelineManager>(m_device, *next->scene_pass);
        auto skybox =
            SkyboxPass::create(m_device, *next->pipelines, m_msaa_samples, m_frame_slot_count);
        if(!skybox)
            return Creation::failure(skybox.error());
        next->skybox_pass = std::move(skybox).value();
        auto materials =
            MaterialRenderer::create(m_device, *next->pipelines, resources, m_frame_slot_count,
                m_msaa_samples, m_material_shaders ? &*m_material_shaders : nullptr, &m_assets);
        if(!materials)
            return Creation::failure(materials.error());
        next->materials = std::move(materials).value();
        auto debug =
            DebugRenderer::create(m_device, *next->pipelines, m_frame_slot_count, m_msaa_samples);
        if(!debug)
            return Creation::failure(debug.error());
        next->debug = std::move(debug).value();
        if(auto graph = rebuild_graph(*next, m_post_process.uses_bloom()); !graph)
            return Creation::failure(graph.error());
        return Creation::success(std::move(next));
    }

    Result<void, GraphicsError> SceneRenderer::rebuild_graph(
        RenderState& state, const bool bloom_enabled) {
        RenderGraph graph;
        const auto shadow = graph.import_image(
            "shadow depth", {.subresources = {.aspects = Flags<ImageAspect>(ImageAspect::Depth)}});
        const auto shadow_pass_id = graph.add_pass(
            {"directional shadow", {{shadow, ResourceUsage::DepthStencilAttachmentWrite, {}}}});
        RenderGraph::Pass scene{"scene", {}};
        std::vector<RenderGraph::ResourceId> scene_attachments;
        RenderGraph::ResourceId output;
        for(const auto& attachment : state.scene_pass->get_attachments()) {
            const bool is_depth = Graphics::is_depth_stencil_format(attachment.description.format);
            auto aspects = Flags<ImageAspect>(is_depth ? ImageAspect::Depth : ImageAspect::Color);
            if(is_depth && !Graphics::is_depth_only_format(attachment.description.format))
                aspects |= ImageAspect::Stencil;
            const auto id = graph.import_image("attachment " + std::to_string(scene.uses.size()),
                {.subresources = {.aspects = aspects}});
            scene_attachments.push_back(id);
            auto usage = ResourceUsage::ColorAttachmentWrite;
            if(is_depth)
                usage = ResourceUsage::DepthStencilAttachmentWrite;
            scene.uses.push_back({id, usage, {}});
            if(!is_depth)
                output = id;
        }
        scene.uses.push_back({shadow, ResourceUsage::SampledRead,
            Flags<PipelineStage>(PipelineStage::FragmentShader)});
        const auto scene_pass_id = graph.add_pass(std::move(scene));
        std::optional<BloomPass::Passes> bloom_passes;
        RenderGraph::Pass display{
            "display", {{output, ResourceUsage::SampledRead,
                           Flags<PipelineStage>(PipelineStage::FragmentShader)}}};
        if(bloom_enabled) {
            bloom_passes = BloomPass::append_passes(graph, output);
            display.uses.push_back({bloom_passes->output, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)});
        }
        const auto output_pass_id = graph.add_pass(std::move(display));
        auto compiled = graph.compile();
        if(!compiled)
            return Result<void, GraphicsError>::failure({compiled.error()});
        state.graph = std::move(compiled).value();
        state.shadow_resource = shadow;
        state.scene_attachments = std::move(scene_attachments);
        state.shadow_pass_id = shadow_pass_id;
        state.scene_pass_id = scene_pass_id;
        state.output_pass_id = output_pass_id;
        state.bloom_passes = bloom_passes;
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::prepare_post_process(
        const PostProcessSettings& settings, const std::chrono::steady_clock::time_point now) {
        if(auto valid = settings.validate(); !valid)
            return Result<void, GraphicsError>::failure({valid.error()});
        if(!m_state)
            return Result<void, GraphicsError>::failure({"Scene renderer is not configured"});
        const auto size = m_state->output_target->get_size();
        if(settings.uses_bloom() == m_post_process.uses_bloom()) {
            m_post_process_failure.reset();
            m_post_process = settings;
            return Result<void, GraphicsError>::success();
        }
        if(m_post_process_failure && m_post_process_failure->size != size)
            m_post_process_failure.reset();
        if(m_post_process_failure && !m_post_process_failure->retry.consume(now))
            return Result<void, GraphicsError>::success();
        const auto prepared = configure_bloom(settings.uses_bloom());
        if(prepared) {
            m_post_process = settings;
            m_post_process_failure.reset();
            return prepared;
        }
        if(!prepared.error().is_out_of_memory())
            return prepared;
        if(!m_post_process_failure)
            m_post_process_failure = TargetRetry{size};
        auto& retry = m_post_process_failure->retry;
        if(!retry.schedule(now))
            LOG_ERROR("Post-process preparation exhausted retries; keeping previous settings: {}",
                prepared.error().message);
        else if(retry.retry_count() == 1)
            LOG_WARN("Post-process preparation will retry; keeping previous settings: {}",
                prepared.error().message);
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::configure_bloom(const bool enabled) {
        std::unique_ptr<BloomPass> candidate;
        if(enabled) {
            auto* bloom = m_state->bloom.get();
            if(!bloom) {
                auto created = BloomPass::create(m_device, m_frame_slot_count);
                if(!created)
                    return Result<void, GraphicsError>::failure(created.error());
                candidate = std::move(created).value();
                bloom = candidate.get();
            }
            if(auto resized = bloom->resize(m_state->output_target->get_size()); !resized)
                return resized;
        }
        if(auto graph = rebuild_graph(*m_state, enabled); !graph)
            return graph;
        if(candidate)
            m_state->bloom = std::move(candidate);
        return Result<void, GraphicsError>::success();
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
        if(m_post_process.uses_bloom()) {
            if(auto bloom = state.bloom->resize(size); !bloom)
                return bloom;
        }
        // 所有候选均成功后再发布，不改变在途帧保留的旧目标。
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
        m_post_process_failure.reset();
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::configure_offscreen(
        RenderResources& resources, Math::Vec2u size) {
        auto next = create_state(resources, nullptr, size);
        if(!next)
            return Result<void, GraphicsError>::failure(next.error());
        m_state = std::move(next).value();
        m_resize_failure.reset();
        m_post_process_failure.reset();
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

    MaterialRenderer::Statistics SceneRenderer::get_material_statistics() const {
        return m_state->materials->get_statistics();
    }

    const PostProcessSettings& SceneRenderer::get_post_process_settings() const {
        return m_post_process;
    }

    RenderTarget& SceneRenderer::get_render_target() {
        return *m_state->output_target;
    }
    const RenderTarget& SceneRenderer::get_render_target() const {
        return *m_state->output_target;
    }

    bool SceneRenderer::is_offscreen() const {
        return m_state && m_state->offscreen;
    }

    void SceneRenderer::skip_frame() {
        m_state->materials->reset_statistics();
    }

    void SceneRenderer::collect_removed_assets(const AssetRegistry& assets) {
        m_state->materials->collect_removed_assets(assets);
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> SceneRenderer::render(
        FrameScheduler& frames, const RenderSubmission& submission, const LineDrawList& lines,
        RenderDiagnostics* diagnostics) {
        PROFILE_SCOPE("SceneRenderer::render");
        using RenderResult = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>;
        frames.retain_current_frame_resource(m_state);
        frames.retain_current_frame_resource(m_state->output_target);
        frames.retain_current_frame_resource(m_state->hdr_target);
        const auto image = frames.get_current_frame_slot_index();
        const auto lighting = ShadowPass::prepare(submission);
        std::vector<RenderGraph::Binding> bindings(m_state->graph.resource_count());
        bindings[m_state->shadow_resource.index] =
            m_state->shadow_pass->get_depth_view(image)->get_image();
        const auto& attachments = m_state->hdr_target->get_framebuffer(image)->get_attachments();
        for(std::size_t index = 0; index < attachments.size(); ++index)
            bindings[m_state->scene_attachments[index].index] = attachments[index]->get_image();
        if(m_state->bloom_passes)
            m_state->bloom->bind_resources(bindings, *m_state->bloom_passes, image);
        std::vector<QueueSemaphoreSubmit> waits;
        const auto recorder = [this, &frames, &submission, &lines, &lighting, &waits](
                                  RenderGraph::PassId pass, CommandBuffer&) {
            return record_pass(pass, frames, submission, lines, lighting, waits);
        };
        auto recorded = Result<void, GraphicsError>::success();
        if(diagnostics)
            recorded = diagnostics->record(m_state->graph, bindings, recorder);
        else
            recorded = m_state->graph.record(frames, bindings, recorder);
        if(!recorded)
            return RenderResult::failure(recorded.error());
        return RenderResult::success(std::move(waits));
    }

    Result<void, GraphicsError> SceneRenderer::record_pass(std::size_t pass, FrameScheduler& frames,
        const RenderSubmission& submission, const LineDrawList& lines, const LightingData& lighting,
        std::vector<QueueSemaphoreSubmit>& waits) {
        const auto slot = frames.get_current_frame_slot_index();
        if(pass == m_state->output_pass_id) {
            const auto hdr = m_state->hdr_target->get_color_view(slot);
            std::shared_ptr<ImageView> bloom;
            if(m_state->bloom_passes)
                bloom = m_state->bloom->get_output(slot);
            return m_state->output_pass->render(
                frames, m_state->output_target, hdr, m_post_process, bloom);
        }
        auto drawn = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::success({});
        if(pass == m_state->shadow_pass_id)
            drawn = m_state->shadow_pass->render(frames, lighting, submission.render_items);
        else if(pass == m_state->scene_pass_id)
            drawn = draw_scene(
                frames, frames.get_current_command_buffer(), submission, lines, lighting);
        else if(m_state->bloom_passes)
            return m_state->bloom->render(frames, pass, *m_state->bloom_passes,
                m_state->hdr_target->get_color_view(slot), m_post_process.bloom_threshold);
        else
            return Result<void, GraphicsError>::failure({"Unknown scene render pass"});
        if(!drawn)
            return Result<void, GraphicsError>::failure(drawn.error());
        for(const auto& wait : drawn.value())
            merge_semaphore_wait(waits, wait);
        return Result<void, GraphicsError>::success();
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> SceneRenderer::draw_scene(
        FrameScheduler& frames, CommandBuffer& command, const RenderSubmission& submission,
        const LineDrawList& lines, const LightingData& lighting) {
        m_state->hdr_target->set_clear_value(
            ClearValue(Math::Vec4(submission.environment.background_color, 1.0f)));
        m_state->hdr_target->begin_render_target(command, frames.get_current_frame_slot_index());
        const auto size = m_state->hdr_target->get_size();
        command.set_viewport(
            Graphics::get_viewport(static_cast<float>(size.x), static_cast<float>(size.y)));
        command.set_scissor(
            Graphics::get_scissor(static_cast<float>(size.x), static_cast<float>(size.y)));
        auto skybox = m_state->skybox_pass->render(frames, submission);
        if(!skybox)
            return skybox;
        auto waits = m_state->materials->render(frames, submission, lighting,
            m_state->shadow_pass->get_depth_view(frames.get_current_frame_slot_index()));
        if(!waits)
            return waits;
        for(const auto& wait : skybox.value())
            merge_semaphore_wait(waits.value(), wait);
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
                m_resize_failure = TargetRetry{size};
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
