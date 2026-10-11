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
        const Window& window, const Settings& settings, AssetRegistry& asset_registry) {
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
        auto programs = std::make_unique<MaterialPrograms>();
        auto& swapchain = context.value()->get_swapchain();
        frames->initialize_swapchain_images(static_cast<uint32_t>(swapchain.get_images().size()));
        auto scene = SceneRenderer::create(
            device, *programs, *resources, settings.vulkan, settings.render, swapchain);
        if(!scene)
            return Creation::failure(scene.error());
        auto renderer = std::unique_ptr<Renderer>(
            new Renderer(std::move(context).value(), std::move(resources), std::move(frames),
                std::move(programs), std::move(scene).value(), asset_registry, settings));
        if(auto enabled = renderer->m_diagnostics->set_enabled(settings.enable_diagnostics);
            !enabled)
            return Creation::failure({enabled.error()});
        renderer->m_output = output;
        return Creation::success(std::move(renderer));
    }

    Renderer::Renderer(std::unique_ptr<RenderContext> context,
        std::unique_ptr<RenderResources> resources, std::unique_ptr<FrameScheduler> frames,
        std::unique_ptr<MaterialPrograms> programs, std::unique_ptr<SceneRenderer> scene,
        AssetRegistry& assets, const Settings& settings)
        : m_render_context(std::move(context)), m_render_resources(std::move(resources)),
          m_frames(std::move(frames)), m_programs(std::move(programs)), m_settings(settings),
          m_scene_resolver(assets), m_asset_registry(assets) {
        m_views.emplace(DEFAULT_VIEW, ViewState{.scene = std::move(scene)});
        m_diagnostics = std::make_unique<RenderDiagnostics>(*m_frames);
        m_presentation = std::make_unique<Presentation>(*m_render_context, *m_frames,
            Presentation::Dependent{
                [this] { get_view(DEFAULT_VIEW).scene->release_presentation_target(); },
                [this](const SwapchainCompatibility& compatibility) {
                    return get_view(DEFAULT_VIEW)
                        .scene->rebuild_presentation_target(
                            *m_render_resources, m_render_context->get_swapchain(), compatibility);
                }});
    }

    Renderer::ViewState& Renderer::get_view(const ViewId id) {
        const auto found = m_views.find(id);
        if(found == m_views.end())
            LOG_FATAL("Unknown render view {}", id);
        return found->second;
    }

    const Renderer::ViewState& Renderer::get_view(const ViewId id) const {
        const auto found = m_views.find(id);
        if(found == m_views.end())
            LOG_FATAL("Unknown render view {}", id);
        return found->second;
    }

    const SceneRenderer& Renderer::get_scene_renderer(const ViewId id) const {
        return *get_view(id).scene;
    }

    Renderer::OffscreenFrame Renderer::get_offscreen_frame(const ViewId id) const {
        const auto& scene = *get_view(id).scene;
        if(!m_frames->is_frame_active() || !scene.is_offscreen())
            LOG_FATAL("Offscreen frame requires an active offscreen renderer frame");
        const auto slot = m_frames->get_current_frame_slot_index();
        return {slot, scene.get_render_target().get_size(), scene.get_offscreen_color_view(slot)};
    }

    Result<Renderer::ViewId, GraphicsError> Renderer::create_offscreen_view(
        const Math::Vec2u initial_size) {
        using Creation = Result<ViewId, GraphicsError>;
        if(m_shutdown_prepared || m_frames->is_frame_active())
            return Creation::failure({"View creation requires a live renderer frame boundary"});
        auto settings = m_settings;
        const auto& quality = get_quality_settings();
        settings.vulkan.msaa_samples = static_cast<SampleCount>(quality.msaa_samples);
        settings.render.max_anisotropy = quality.max_anisotropy;
        settings.render.render_scale = quality.render_scale;
        settings.render.hdr_headroom = m_output.hdr_headroom;
        settings.render.hdr_white_level = m_output.hdr_white_level;
        auto scene = SceneRenderer::create(m_render_context->get_device(), *m_programs,
            *m_render_resources, settings.vulkan, settings.render, initial_size,
            get_view(DEFAULT_VIEW).scene->get_shadow_pass());
        if(!scene)
            return Creation::failure(scene.error());
        const auto id = m_next_view_id++;
        m_views.emplace(
            id, ViewState{.scene = std::move(scene).value(), .view = {.visible = false}});
        return Creation::success(id);
    }

    Result<void, GraphicsError> Renderer::remove_offscreen_view(const ViewId id) {
        if(m_shutdown_prepared || m_frames->is_frame_active())
            return Result<void, GraphicsError>::failure(
                {"View removal requires a live renderer frame boundary"});
        if(id == DEFAULT_VIEW || !m_views.contains(id))
            return Result<void, GraphicsError>::failure(
                {"Cannot remove the default or unknown view"});
        m_views.erase(id);
        return Result<void, GraphicsError>::success();
    }

    uint32_t Renderer::max_render_target_dimension() const {
        const auto limit = m_render_context->get_device().get_capability().max_image_dimension_2d;
        if(limit == 0)
            LOG_FATAL("Selected Vulkan device has no valid 2D image dimension limit");
        return limit;
    }

    std::vector<std::shared_ptr<const MaterialLayout>> Renderer::get_material_layouts() const {
        return get_view(DEFAULT_VIEW).scene->get_material_layouts();
    }

    Result<Renderer::FramePreparation, GraphicsError> Renderer::prepare_frame() {
        return prepare_frame(take_asset_removals());
    }

    AssetRegistry::Removals Renderer::take_asset_removals() {
        m_scene_resolver.refresh_assets();
        return m_asset_registry.take_removals();
    }

    std::shared_ptr<const RenderWorld> Renderer::resolve_scene(const RenderScene* scene) {
        PROFILE_SCOPE("resolve render assets");
        if(!scene || !has_visible_view()) {
            m_scene_resolver.reset();
            return {};
        }
        return RenderDiagnostics::measure_preparation(m_diagnostics.get(),
            RenderDiagnostics::PreparationPhase::Assets,
            [&] { return m_scene_resolver.update(*scene); });
    }

    Result<Renderer::FramePreparation, GraphicsError> Renderer::prepare_frame(
        AssetRegistry::Removals removals) {
        using Preparation = Result<FramePreparation, GraphicsError>;
        if(m_shutdown_prepared)
            return Preparation::failure({"Renderer is shutting down"});
        ScopeExit failed([this] { prepare_shutdown(); });
        if(m_frames->is_frame_active())
            return Preparation::failure({"A render frame is already active"});
        PROFILE_SCOPE("prepare frame");
        m_render_resources->collect_completed_uploads();
        m_render_context->get_device().collect_retired_resources();
        m_programs->remove_assets(removals);
        for(auto& [id, view] : m_views)
            view.scene->remove_assets(removals);
        m_geometry.clear_frame();
        m_world.reset();

        if(m_pending_output) {
            const auto output = std::exchange(m_pending_output, std::nullopt).value();
            if(auto calibrated = get_view(DEFAULT_VIEW)
                    .scene->configure_output_calibration(
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
            const auto quality = std::exchange(m_pending_quality, std::nullopt).value();
            std::vector<std::shared_ptr<SceneRenderer::RenderState>> candidates;
            candidates.reserve(m_views.size());
            for(auto& [id, view] : m_views) {
                auto candidate = view.scene->prepare_quality(
                    *m_render_resources, m_render_context->get_swapchain(), quality);
                if(!candidate) {
                    m_quality_error = candidate.error().message;
                    if(candidate.error().is_device_lost())
                        return Preparation::failure(candidate.error());
                    LOG_WARN("Keeping previous render quality: {}", m_quality_error);
                    break;
                }
                candidates.push_back(std::move(candidate).value());
            }
            if(candidates.size() == m_views.size()) {
                auto candidate = candidates.begin();
                for(auto& [id, view] : m_views)
                    view.scene->install_quality(std::move(*candidate++), quality);
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
        return render_world(resolve_scene(&render_scene));
    }

    Result<void, GraphicsError> Renderer::render_frame() {
        return render_world(resolve_scene(nullptr));
    }

    Result<void, GraphicsError> Renderer::render_world(std::shared_ptr<const RenderWorld> frame) {
        auto result = record_world(std::move(frame));
        if(!result)
            prepare_shutdown();
        return result;
    }

    bool Renderer::has_visible_view() const {
        return std::ranges::any_of(m_views, [](const auto& entry) {
            return entry.second.view.visible || !entry.second.scene->is_offscreen();
        });
    }

    Result<void, GraphicsError> Renderer::record_world(std::shared_ptr<const RenderWorld> frame) {
        if(!m_diagnostics->is_enabled())
            return draw_world(std::move(frame));
        const auto serial = m_frames->get_current_frame_serial();
        const auto start = RenderDiagnostics::Clock::now();
        auto result = draw_world(std::move(frame));
        if(result)
            m_diagnostics->record_frame(serial,
                std::chrono::duration<double, std::milli>(RenderDiagnostics::Clock::now() - start)
                    .count());
        return result;
    }

    Result<void, GraphicsError> Renderer::draw_world(std::shared_ptr<const RenderWorld> frame) {
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        if(!m_frames->is_recording_frame())
            return Result<void, GraphicsError>::failure({"No prepared render frame"});
        PROFILE_SCOPE("render frame");
        if(!has_visible_view()) {
            discard_frame_requests();
            get_view(DEFAULT_VIEW).scene->get_shadow_pass()->reset_statistics();
            for(auto& [id, view] : m_views)
                view.scene->skip_frame();
            m_diagnostics->skip_frame();
            std::vector<QueueSemaphoreSubmit> waits;
            if(auto recorded = record_overlay(waits); !recorded)
                return recorded;
            return m_presentation->end_frame(waits);
        }
        m_world = std::move(frame);
        const RenderWorld empty_world;
        const auto& world = m_world ? *m_world : empty_world;
        RenderDiagnostics::measure_preparation(m_diagnostics.get(),
            RenderDiagnostics::PreparationPhase::Geometry, [&] { m_geometry.prepare(world); });
        std::vector<MaterialRenderer*> materials;
        materials.reserve(m_views.size());
        for(auto& [id, view] : m_views)
            materials.push_back(&view.scene->get_material_renderer());
        if(auto programs = RenderDiagnostics::measure_preparation(m_diagnostics.get(),
               RenderDiagnostics::PreparationPhase::MaterialPrograms,
               [&] { return MaterialRenderer::prepare_programs(materials, world); });
            !programs)
            return programs;
        std::vector<QueueSemaphoreSubmit> resource_waits;
        // 诊断查询覆盖第一个可见 View；其他 View 不重复开启同一组 GPU 查询。
        bool diagnosed = false;
        for(auto& [id, view] : m_views) {
            if(!view.view.visible && view.scene->is_offscreen()) {
                view.pick_request.reset();
                view.pick_result.reset();
                view.lines.clear();
                view.scene->skip_frame();
                continue;
            }
            auto* diagnostics = diagnosed ? nullptr : m_diagnostics.get();
            if(auto recorded =
                    render_view(view, world, m_world != nullptr, resource_waits, diagnostics);
                !recorded)
                return recorded;
            diagnosed = true;
        }
        if(auto recorded = record_overlay(resource_waits); !recorded)
            return recorded;

        return m_presentation->end_frame(resource_waits, m_diagnostics.get());
    }

    Result<void, GraphicsError> Renderer::render_view(ViewState& view, const RenderWorld& world,
        const bool has_scene, std::vector<QueueSemaphoreSubmit>& waits,
        RenderDiagnostics* diagnostics) {
        RenderView frame_view = view.view;
        frame_view.render_size = view.scene->get_render_target().get_size();
        const RenderSubmission submission{
            world, has_scene ? view.camera.resolve(world, frame_view) : std::nullopt};
        if(auto prepared = view.scene->prepare_post_process(world.post_process); !prepared)
            return prepared;
        const auto pick_request = std::exchange(view.pick_request, std::nullopt);
        if(pick_request && has_scene && frame_view.visible
            && pick_request->scene_lifetime == world.scene_lifetime
            && pick_request->image_resolution == frame_view.render_size)
            view.pick_result = ViewportPickResult{world.scene_lifetime,
                pick_render_submission(submission, pick_request->pixel, frame_view.render_size)};
        if(!frame_view.visible)
            view.lines.clear();
        auto resource_waits =
            view.scene->render(*m_frames, submission, m_geometry, view.lines, diagnostics);
        view.lines.clear();
        if(!resource_waits)
            return Result<void, GraphicsError>::failure(resource_waits.error());
        for(const auto& wait : resource_waits.value())
            merge_semaphore_wait(waits, wait);
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> Renderer::enable_offscreen_rendering(
        const Math::Vec2u initial_size) {
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        if(m_frames->is_frame_active())
            return Result<void, GraphicsError>::failure(
                {"Target configuration requires a frame boundary"});
        return get_view(DEFAULT_VIEW).scene->configure_offscreen(*m_render_resources, initial_size);
    }

    Result<MaterialRenderer::ReloadReport, GraphicsError> Renderer::reload_material_shaders(
        MaterialShaders shaders) {
        if(m_shutdown_prepared)
            return Result<MaterialRenderer::ReloadReport, GraphicsError>::failure(
                {"Renderer is shutting down"});
        if(m_frames->is_frame_active())
            return Result<MaterialRenderer::ReloadReport, GraphicsError>::failure(
                {"Shader publication requires a frame boundary"});
        std::vector<MaterialRenderer::ShaderUpdate> candidates;
        MaterialRenderer::ReloadReport report;
        for(auto& [id, view] : m_views) {
            auto candidate = view.scene->prepare_material_shaders(shaders);
            if(!candidate) {
                auto previous = default_material_shaders();
                if(const auto* overrides = m_programs->builtin_overrides())
                    merge_material_shaders(previous, *overrides);
                std::vector<std::string> failed;
                for(const auto& [name, code] : shaders) {
                    const auto prior = previous.find(name);
                    if(prior == previous.end() || prior->second != code)
                        failed.push_back(name);
                }
                invalidate_material_shaders(failed);
                return Result<MaterialRenderer::ReloadReport, GraphicsError>::failure(
                    candidate.error());
            }
            const auto& prepared = candidate.value().report();
            report.pipelines += prepared.pipelines;
            report.material_versions += prepared.material_versions;
            report.material_bindings += prepared.material_bindings;
            report.pipeline_preparation_ms += prepared.pipeline_preparation_ms;
            report.candidate_copy_ms += prepared.candidate_copy_ms;
            report.material_cpu_ms += prepared.material_cpu_ms;
            report.material_gpu_ms += prepared.material_gpu_ms;
            candidates.push_back(std::move(candidate).value());
        }
        for(auto& candidate : candidates)
            std::move(candidate).publish();
        m_programs->publish_builtin(std::move(shaders));
        return Result<MaterialRenderer::ReloadReport, GraphicsError>::success(report);
    }

    void Renderer::invalidate_material_shaders(const std::span<const std::string> programs) {
        m_programs->invalidate_builtin(programs);
    }

    void Renderer::MaterialUpdate::publish() && {
        for(auto& update : m_views)
            std::move(update).publish();
        m_views.clear();
    }

    Result<Renderer::MaterialUpdate, GraphicsError> Renderer::prepare_material_update(
        const AssetHandle handle, const std::shared_ptr<const Material>& snapshot,
        const std::shared_ptr<const ShaderProgramArtifact>& program_source) {
        using Preparation = Result<MaterialUpdate, GraphicsError>;
        if(m_shutdown_prepared)
            return Preparation::failure({"Renderer is shutting down"});
        if(m_frames->is_frame_active())
            return Preparation::failure({"Material preparation requires a frame boundary"});
        std::vector<MaterialRenderer*> materials;
        materials.reserve(m_views.size());
        for(auto& [id, view] : m_views) {
            materials.push_back(&view.scene->get_material_renderer());
        }
        auto candidates =
            MaterialRenderer::prepare_material_updates(materials, handle, snapshot, program_source);
        if(!candidates)
            return Preparation::failure(candidates.error());
        MaterialUpdate update;
        update.m_views = std::move(candidates).value();
        return Preparation::success(std::move(update));
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
        if(get_view(DEFAULT_VIEW).scene->is_offscreen())
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
        if(auto valid = get_view(DEFAULT_VIEW).scene->validate_quality_settings(settings); !valid)
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
        return get_view(DEFAULT_VIEW).scene->get_quality_settings();
    }

    Math::Vec2u Renderer::get_scene_size() const {
        return get_view(DEFAULT_VIEW).scene->get_scene_size();
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

    Result<void, GraphicsError> Renderer::set_render_view(RenderView view, const ViewId id) {
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        const auto found = m_views.find(id);
        if(found == m_views.end())
            return Result<void, GraphicsError>::failure({"Unknown render view"});
        if(view.visible && view.render_size.x > 0 && view.render_size.y > 0) {
            if(auto resized = found->second.scene->resize_offscreen_target(view.render_size);
                !resized) {
                prepare_shutdown();
                return resized;
            }
        }
        found->second.view = std::move(view);
        return Result<void, GraphicsError>::success();
    }

    void Renderer::request_viewport_pick(const Math::Vec2u pixel,
        const Math::Vec2u image_resolution, const uint64_t scene_lifetime, const ViewId id) {
        auto& view = get_view(id);
        view.pick_result.reset();
        view.pick_request = ViewportPickRequest{pixel, image_resolution, scene_lifetime};
    }

    std::optional<Renderer::ViewportPickResult> Renderer::take_viewport_pick_result(
        const ViewId id) {
        if(m_frames->is_frame_active())
            return std::nullopt;
        return std::exchange(get_view(id).pick_result, std::nullopt);
    }

    void Renderer::cancel_viewport_pick(const ViewId id) {
        auto& view = get_view(id);
        view.pick_request.reset();
        view.pick_result.reset();
    }

    void Renderer::submit_lines(const LineDrawList& draw_list, const ViewId id) {
        if(!get_view(id).lines.append(draw_list))
            LOG_WARN("Debug line batch rejected: vertex capacity exceeded");
    }

    void Renderer::discard_frame_requests() {
        for(auto& [id, view] : m_views) {
            view.pick_request.reset();
            view.pick_result.reset();
            view.lines.clear();
        }
        m_geometry.clear_frame();
        m_world.reset();
    }

    void Renderer::prepare_shutdown() noexcept {
        if(std::exchange(m_shutdown_prepared, true))
            return;
        m_render_context->get_device().wait_idle_for_shutdown();
        discard_frame_requests();
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        prepare_shutdown();
        m_scene_resolver.reset();

        m_presentation.reset();
        m_diagnostics.reset();
        m_frames.reset();
        m_views.clear();
        m_render_resources.reset();
        m_render_context.reset();
    }
}
