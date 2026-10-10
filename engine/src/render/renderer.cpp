#include "renderer.h"
#include "asset/registry.h"
#include "common/scope_exit.h"
#include "render/render_context.h"
#include "render/frame_scheduler.h"
#include "render/presentation.h"
#include "render/overlay_record_context.h"
#include "render/render_diagnostics.h"
#include "render/material/material_programs.h"
#include "render/resource/render_resources.h"
#include "render/scene/scene_renderer.h"
#include "core/window.h"
#include "graphics/device.h"
#include "graphics/swapchain.h"
#include "graphics/convert.h"
#include "render/render_target.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"

#include <algorithm>
#include <utility>

namespace Comet {
    Result<std::unique_ptr<Renderer>, GraphicsError> Renderer::create(
        const Window& window, const Settings& settings, const AssetRegistry& asset_registry) {
        using Creation = Result<std::unique_ptr<Renderer>, GraphicsError>;
        if(settings.render.max_frames_in_flight == 0)
            return Creation::failure({"Renderer requires at least one frame slot"});
        const OutputSettings output{settings.render.output_mode, settings.render.hdr_headroom,
            settings.render.hdr_white_level};
        if(auto valid = output.validate(); !valid)
            return Creation::failure({valid.error()});
        auto context = RenderContext::create(window, settings.vulkan, settings.render);
        if(!context)
            return Creation::failure(context.error());
        auto& device = context.value()->get_device();
        auto resources = std::make_unique<RenderResources>(device);
        auto frames =
            std::make_unique<FrameScheduler>(device, settings.render.max_frames_in_flight);
        auto programs = std::make_unique<MaterialPrograms>(asset_registry);
        auto& swapchain = context.value()->get_swapchain();
        frames->initialize_swapchain_images(static_cast<uint32_t>(swapchain.get_images().size()));
        auto scene = SceneRenderer::create(
            device, *programs, *resources, settings.vulkan, settings.render, swapchain);
        if(!scene)
            return Creation::failure(scene.error());
        auto renderer =
            std::unique_ptr<Renderer>(new Renderer(std::move(context).value(), std::move(resources),
                std::move(frames), std::move(programs), std::move(scene).value(), asset_registry));
        if(auto enabled = renderer->m_diagnostics->set_enabled(settings.enable_diagnostics);
            !enabled)
            return Creation::failure({enabled.error()});
        renderer->m_output = output;
        return Creation::success(std::move(renderer));
    }

    Renderer::Renderer(std::unique_ptr<RenderContext> context,
        std::unique_ptr<RenderResources> resources, std::unique_ptr<FrameScheduler> frames,
        std::unique_ptr<MaterialPrograms> programs, std::unique_ptr<SceneRenderer> scene,
        const AssetRegistry& assets)
        : m_render_context(std::move(context)), m_render_resources(std::move(resources)),
          m_frames(std::move(frames)), m_programs(std::move(programs)),
          m_scene_renderer(std::move(scene)), m_scene_resolver(assets), m_asset_registry(assets) {
        m_diagnostics = std::make_unique<RenderDiagnostics>(*m_frames);
        m_presentation = std::make_unique<Presentation>(*m_render_context, *m_frames,
            Presentation::Dependent{[this] { m_scene_renderer->release_presentation_target(); },
                [this](const SwapchainCompatibility& compatibility) {
                    return m_scene_renderer->rebuild_presentation_target(
                        *m_render_resources, m_render_context->get_swapchain(), compatibility);
                }});
    }

    Renderer::OffscreenFrame Renderer::get_offscreen_frame() const {
        if(!m_frames->is_frame_active() || !m_scene_renderer->is_offscreen())
            LOG_FATAL("Offscreen frame requires an active offscreen renderer frame");
        const auto slot = m_frames->get_current_frame_slot_index();
        return {slot, m_scene_renderer->get_render_target().get_size(),
            m_scene_renderer->get_offscreen_color_view(slot)};
    }

    uint32_t Renderer::max_render_target_dimension() const {
        const auto limit = m_render_context->get_device().get_capability().max_image_dimension_2d;
        if(limit == 0)
            LOG_FATAL("Selected Vulkan device has no valid 2D image dimension limit");
        return limit;
    }

    std::vector<std::shared_ptr<const MaterialLayout>> Renderer::get_material_layouts() const {
        return m_scene_renderer->get_material_layouts();
    }

    Result<Renderer::FramePreparation, GraphicsError> Renderer::prepare_frame() {
        using Preparation = Result<FramePreparation, GraphicsError>;
        if(m_shutdown_prepared)
            return Preparation::failure({"Renderer is shutting down"});
        ScopeExit failed([this] { prepare_shutdown(); });
        if(m_frames->is_frame_active())
            return Preparation::failure({"A render frame is already active"});
        PROFILE_SCOPE("prepare frame");
        m_render_resources->collect_completed_uploads();
        m_programs->collect_removed();
        m_scene_renderer->collect_removed_assets(m_asset_registry);
        m_scene_resolver.refresh_assets(m_submission);

        if(m_pending_output) {
            const auto output = std::exchange(m_pending_output, std::nullopt).value();
            if(auto calibrated = m_scene_renderer->configure_output_calibration(
                   output.hdr_headroom, output.hdr_white_level);
                !calibrated)
                return Preparation::failure(calibrated.error());
            if(m_render_context->get_swapchain().request_output_mode(output.mode)) {
                m_presentation->request_recreation();
                m_output_recreating = true;
            }
            m_output = output;
        }
        auto preparation = m_presentation->begin_frame();
        if(!preparation) {
            discard_frame_requests();
            return Preparation::failure(preparation.error());
        }
        if(preparation.value())
            m_output_recreating = false;
        if(preparation.value() && m_pending_quality) {
            const auto quality = std::exchange(m_pending_quality, std::nullopt);
            auto applied = m_scene_renderer->configure_quality(
                *m_render_resources, m_render_context->get_swapchain(), *quality);
            if(!applied) {
                m_quality_error = applied.error().message;
                if(applied.error().is_device_lost())
                    return Preparation::failure(applied.error());
                LOG_WARN("Keeping previous render quality: {}", m_quality_error);
            }
        }
        if(auto collected = m_diagnostics->collect_completed(); !collected)
            return Preparation::failure(collected.error());
        m_diagnostics->poll_memory();
        auto status = FramePreparation::Ready;
        if(!preparation.value()) {
            status = FramePreparation::Deferred;
            discard_frame_requests();
        }
        failed.release();
        return Preparation::success(status);
    }

    Result<void, GraphicsError> Renderer::render_frame(const RenderScene& render_scene) {
        return complete_frame(&render_scene);
    }

    Result<void, GraphicsError> Renderer::render_frame() {
        return complete_frame(nullptr);
    }

    Result<void, GraphicsError> Renderer::complete_frame(const RenderScene* render_scene) {
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        ScopeExit failed([this] { prepare_shutdown(); });
        if(!m_frames->is_recording_frame())
            return Result<void, GraphicsError>::failure({"No prepared render frame"});
        PROFILE_SCOPE("render frame");
        if(!m_render_view.visible && m_scene_renderer->is_offscreen()) {
            discard_frame_requests();
            m_scene_renderer->skip_frame();
            m_diagnostics->skip_frame();
            std::vector<QueueSemaphoreSubmit> waits;
            if(auto recorded = record_overlay(waits); !recorded)
                return recorded;
            auto submitted = m_presentation->end_frame(waits);
            if(submitted)
                failed.release();
            return submitted;
        }
        RenderView frame_view = m_render_view;
        frame_view.render_size = m_scene_renderer->get_render_target().get_size();
        auto& submission = m_submission;
        if(render_scene)
            RenderDiagnostics::measure_preparation(m_diagnostics.get(),
                RenderDiagnostics::PreparationPhase::Assets,
                [&] { m_scene_resolver.resolve(*render_scene, frame_view, submission); });
        else {
            submission.render_items.clear();
            submission.environment_resource.reset();
            submission.environment_revision = 0;
            submission.scene_lifetime = 0;
            submission.asset_revision = 0;
            submission.view_project_matrix.reset();
            submission.lights.clear();
            submission.environment = {};
            submission.post_process = {};
        }
        if(auto programs = RenderDiagnostics::measure_preparation(m_diagnostics.get(),
               RenderDiagnostics::PreparationPhase::MaterialPrograms,
               [&] { return m_scene_renderer->prepare_material_programs(submission); });
            !programs)
            return programs;
        if(auto prepared = m_scene_renderer->prepare_post_process(submission.post_process);
            !prepared)
            return prepared;
        const auto pick_request = std::exchange(m_viewport_pick_request, std::nullopt);
        if(pick_request && frame_view.visible
            && pick_request->image_resolution == frame_view.render_size
            && m_viewport_pick_callback) {
            m_viewport_pick_callback(
                pick_render_submission(submission, pick_request->pixel, frame_view.render_size));
        }
        if(!frame_view.visible) {
            m_line_draw_list.clear();
        }
        auto resource_waits =
            m_scene_renderer->render(*m_frames, submission, m_line_draw_list, m_diagnostics.get());
        m_line_draw_list.clear();

        if(!resource_waits) {
            return Result<void, GraphicsError>::failure(resource_waits.error());
        }

        if(auto recorded = record_overlay(resource_waits.value()); !recorded)
            return recorded;

        auto submitted = m_presentation->end_frame(resource_waits.value());
        if(submitted)
            failed.release();
        return submitted;
    }

    Result<void, GraphicsError> Renderer::enable_offscreen_rendering(
        const Math::Vec2u initial_size) {
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        if(m_frames->is_frame_active())
            return Result<void, GraphicsError>::failure(
                {"Target configuration requires a frame boundary"});
        return m_scene_renderer->configure_offscreen(*m_render_resources, initial_size);
    }

    Result<MaterialRenderer::ReloadReport, GraphicsError> Renderer::reload_material_shaders(
        MaterialShaders shaders) {
        if(m_shutdown_prepared)
            return Result<MaterialRenderer::ReloadReport, GraphicsError>::failure(
                {"Renderer is shutting down"});
        if(m_frames->is_frame_active())
            return Result<MaterialRenderer::ReloadReport, GraphicsError>::failure(
                {"Shader publication requires a frame boundary"});
        return m_scene_renderer->reload_material_shaders(std::move(shaders));
    }

    Result<MaterialRenderer::MaterialUpdate, GraphicsError> Renderer::prepare_material_update(
        const AssetHandle handle, const std::shared_ptr<const Material>& material) {
        if(m_shutdown_prepared)
            return Result<MaterialRenderer::MaterialUpdate, GraphicsError>::failure(
                {"Renderer is shutting down"});
        if(m_frames->is_frame_active())
            return Result<MaterialRenderer::MaterialUpdate, GraphicsError>::failure(
                {"Material preparation requires a frame boundary"});
        return m_scene_renderer->prepare_material_update(handle, material);
    }

    void Renderer::set_vsync_enabled(bool enabled) {
        if(m_render_context->get_swapchain().request_vsync(enabled))
            request_swapchain_recreation();
    }

    bool Renderer::is_vsync_enabled() const {
        return m_render_context->get_swapchain().get_active_generation()->get_config().present_mode
               != vk::PresentModeKHR::eImmediate;
    }

    Result<void, GraphicsError> Renderer::request_output_settings(OutputSettings settings) {
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        if(m_scene_renderer->is_offscreen())
            return Result<void, GraphicsError>::failure(
                {"Display output settings require a standalone presentation"});
        if(auto valid = settings.validate(); !valid)
            return Result<void, GraphicsError>::failure({valid.error()});
        m_pending_output.reset();
        if(settings != m_output)
            m_pending_output = settings;
        return Result<void, GraphicsError>::success();
    }

    bool Renderer::is_hdr_output() const {
        const auto& config =
            m_render_context->get_swapchain().get_active_generation()->get_config();
        return config.surface_format.colorSpace == vk::ColorSpaceKHR::eExtendedSrgbLinearEXT;
    }

    void Renderer::request_swapchain_recreation() {
        m_presentation->request_recreation();
    }

    Result<QualitySettings, GraphicsError> Renderer::resolve_quality_settings(
        QualitySettings settings) const {
        if(auto valid = settings.validate(); !valid)
            return Result<QualitySettings, GraphicsError>::failure({valid.error()});
        settings.max_anisotropy = std::min(settings.max_anisotropy, max_anisotropy());
        if(auto valid = m_scene_renderer->validate_quality_settings(settings); !valid)
            return Result<QualitySettings, GraphicsError>::failure(valid.error());
        return Result<QualitySettings, GraphicsError>::success(settings);
    }

    Result<void, GraphicsError> Renderer::request_quality_settings(QualitySettings settings) {
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        auto resolved = resolve_quality_settings(settings);
        if(!resolved)
            return Result<void, GraphicsError>::failure(resolved.error());
        m_quality_error.clear();
        m_pending_quality.reset();
        if(resolved.value() != get_quality_settings())
            m_pending_quality = resolved.value();
        return Result<void, GraphicsError>::success();
    }

    const QualitySettings& Renderer::get_quality_settings() const {
        return m_scene_renderer->get_quality_settings();
    }

    Math::Vec2u Renderer::get_scene_size() const {
        return m_scene_renderer->get_scene_size();
    }

    std::vector<uint32_t> Renderer::supported_msaa_samples() const {
        std::vector<uint32_t> samples;
        for(const uint32_t count : {1u, 2u, 4u, 8u}) {
            auto settings = get_quality_settings();
            settings.msaa_samples = count;
            if(resolve_quality_settings(settings))
                samples.push_back(count);
        }
        return samples;
    }

    float Renderer::max_anisotropy() const {
        return m_render_context->get_device().get_capability().max_sampler_anisotropy;
    }

    void Renderer::wait_idle() {
        // 关闭准备已等待设备；失败遗留的未提交帧不能再按正常帧等待。
        if(m_shutdown_prepared)
            return;
        m_frames->wait_for_all_slots();
        m_render_context->get_device().get_present_queue(0).wait_idle();
    }

    void Renderer::set_overlay(Overlay overlay) {
        m_render_overlay = std::move(overlay.render);
        m_presentation->set_overlay({std::move(overlay.release), std::move(overlay.rebuild)});
    }

    Result<void, GraphicsError> Renderer::record_overlay(std::vector<QueueSemaphoreSubmit>& waits) {
        if(!m_render_overlay)
            return Result<void, GraphicsError>::success();
        OverlayRecordContext context(*m_frames, waits);
        return m_render_overlay(context);
    }

    Result<void, GraphicsError> Renderer::set_render_view(RenderView view) {
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        if(view.visible && view.render_size.x > 0 && view.render_size.y > 0) {
            if(auto resized = m_scene_renderer->resize_offscreen_target(view.render_size);
                !resized) {
                prepare_shutdown();
                return resized;
            }
        }
        m_render_view = std::move(view);
        return Result<void, GraphicsError>::success();
    }

    void Renderer::request_viewport_pick(
        const Math::Vec2u pixel, const Math::Vec2u image_resolution) {
        m_viewport_pick_request = ViewportPickRequest{pixel, image_resolution};
    }

    void Renderer::set_viewport_pick_callback(ViewportPickCallback callback) {
        m_viewport_pick_callback = std::move(callback);
        m_viewport_pick_request.reset();
    }

    void Renderer::submit_lines(const LineDrawList& draw_list) {
        if(!m_line_draw_list.append(draw_list))
            LOG_WARN("Debug line batch rejected: vertex capacity exceeded");
    }

    void Renderer::discard_frame_requests() {
        m_viewport_pick_request.reset();
        m_line_draw_list.clear();
        m_submission.render_items.clear();
        m_submission.environment_resource.reset();
        m_submission.environment_revision = 0;
    }

    void Renderer::prepare_shutdown() noexcept {
        if(std::exchange(m_shutdown_prepared, true))
            return;
        m_render_context->get_device().wait_idle_for_shutdown();
        m_submission.render_items.clear();
        m_submission.environment_resource.reset();
        m_submission.environment_revision = 0;
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        prepare_shutdown();

        m_presentation.reset();
        m_diagnostics.reset();
        m_frames.reset();
        m_scene_renderer.reset();
        m_render_resources.reset();
        m_render_context.reset();
    }
}
