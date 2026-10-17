#include "render/passes/skinning_pass.h"
#include "render/passes/realtime_irradiance_pass.h"
#include "render/resource/mesh.h"
#include "renderer.h"
#include "render/render_thread.h"
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
        const Window& window, const Settings& settings) {
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
            new Renderer(window, std::move(context).value(), std::move(resources),
                std::move(frames), std::move(programs), std::move(scene).value(), settings));
        if(auto enabled = renderer->m_diagnostics->set_enabled(settings.enable_diagnostics);
            !enabled)
            return Creation::failure({enabled.error()});
        renderer->m_output = output;
        return Creation::success(std::move(renderer));
    }

    Renderer::Renderer(const Window& window, std::unique_ptr<RenderContext> context,
        std::unique_ptr<RenderResources> resources, std::unique_ptr<FrameScheduler> frames,
        std::unique_ptr<MaterialPrograms> programs, std::unique_ptr<SceneRenderer> scene,
        const Settings& settings)
        : m_window(window), m_render_context(std::move(context)),
          m_render_resources(std::move(resources)), m_frames(std::move(frames)),
          m_programs(std::move(programs)), m_settings(settings) {
        m_views.emplace(DEFAULT_VIEW, ViewState{.scene = std::move(scene)});
        const auto irradiance_support =
            RealtimeIrradiancePass::check_support(m_render_context->get_device());
        if(!irradiance_support)
            m_irradiance_support_error = irradiance_support.error().message;
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
        if(m_render_thread && !m_render_thread->is_current())
            return m_render_thread->invoke(
                [this, initial_size] { return create_offscreen_view(initial_size); });
        using Creation = Result<ViewId, GraphicsError>;
        if(m_shutdown_prepared || m_frames->is_frame_active())
            return Creation::failure({"View creation requires a live renderer frame boundary"});
        auto settings = m_settings;
        const auto& quality = get_quality_settings();
        settings.vulkan.msaa_samples = static_cast<SampleCount>(quality.msaa_samples);
        settings.render.max_anisotropy = quality.max_anisotropy;
        settings.render.render_scale = quality.render_scale;
        settings.render.temporal_aa = quality.temporal_aa;
        settings.render.hdr_headroom = m_output.hdr_headroom;
        settings.render.hdr_white_level = m_output.hdr_white_level;
        auto scene = SceneRenderer::create(m_render_context->get_device(), *m_programs,
            *m_render_resources, settings.vulkan, settings.render, initial_size);
        if(!scene)
            return Creation::failure(scene.error());
        const auto id = m_next_view_id++;
        m_views.emplace(
            id, ViewState{.scene = std::move(scene).value(), .view = {.visible = false}});
        return Creation::success(id);
    }

    Result<void, GraphicsError> Renderer::remove_offscreen_view(const ViewId id) {
        if(m_render_thread && !m_render_thread->is_current())
            return m_render_thread->invoke([this, id] { return remove_offscreen_view(id); });
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

    uint32_t Renderer::path_trace_samples(const ViewId id) const {
        return get_view(id).scene->get_path_trace_statistics().samples;
    }

    Result<void, GraphicsError> Renderer::check_path_trace_support() const {
        return PathTracePass::check_support(m_render_context->get_device());
    }

    std::vector<std::shared_ptr<const MaterialLayout>> Renderer::get_material_layouts() const {
        return get_view(DEFAULT_VIEW).scene->get_material_layouts();
    }

    Result<void, GraphicsError> Renderer::begin_render_loop() {
        return m_render_thread->invoke([this] {
            m_render_context->get_device().adopt_resource_thread();
            return Result<void, GraphicsError>::success();
        });
    }

    void Renderer::end_render_loop() {
        // Engine 已排空 CPU 帧；帧槽及上传批次继续保活尚在 GPU 上使用的资源。
        m_render_context->get_device().adopt_resource_thread();
    }

    void Renderer::collect_resources() {
        m_render_resources->collect_completed_uploads();
        m_render_context->get_device().collect_retired_resources();
    }

    Result<Renderer::FramePreparation, GraphicsError> Renderer::prepare_frame(
        AssetRegistry::Removals removals) {
        const auto size = m_window.get_framebuffer_size();
        if(m_render_thread && !m_render_context->get_device().is_resource_owner_thread()) {
            const auto prepared = m_render_thread->prepare_frame(size, std::move(removals));
            if(!prepared)
                return Result<FramePreparation, GraphicsError>::failure(prepared.error());
            return Result<FramePreparation, GraphicsError>::success(
                prepared.value() ? FramePreparation::Ready : FramePreparation::Deferred);
        }
        return prepare_gpu_frame(size, std::move(removals));
    }

    Result<Renderer::FramePreparation, GraphicsError> Renderer::prepare_gpu_frame(
        const Math::Vec2u framebuffer_size, AssetRegistry::Removals removals) {
        using Preparation = Result<FramePreparation, GraphicsError>;
        m_frame_preparation_ms = 0;
        const bool diagnosed =
            m_render_thread && m_render_thread->is_current() && m_diagnostics->is_enabled();
        RenderDiagnostics::Clock::time_point start{};
        if(diagnosed)
            start = RenderDiagnostics::Clock::now();
        const ScopeExit measure([&] {
            if(diagnosed)
                m_frame_preparation_ms = std::chrono::duration<double, std::milli>(
                    RenderDiagnostics::Clock::now() - start)
                                             .count();
        });
        if(m_shutdown_prepared)
            return Preparation::failure({"Renderer is shutting down"});
        ScopeExit failed([this] { prepare_shutdown(); });
        if(m_frames->is_frame_active())
            return Preparation::failure({"A render frame is already active"});
        PROFILE_SCOPE("prepare frame");
        if(auto maintained = maintain_assets(std::move(removals)); !maintained)
            return Preparation::failure(maintained.error());

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
        auto preparation = m_presentation->begin_frame(framebuffer_size);
        if(!preparation) {
            discard_frame_requests();
            return Preparation::failure(preparation.error());
        }
        if(preparation.value())
            m_output_recreating = false;
        if(preparation.value()) {
            if(auto applied = apply_pending_quality(); !applied)
                return Preparation::failure(applied.error());
        }
        if(auto collected = m_diagnostics->collect_completed(); !collected)
            return Preparation::failure(collected.error());
        if(m_render_context->get_device().is_resource_owner_thread())
            m_diagnostics->poll_memory();
        auto status = FramePreparation::Ready;
        if(!preparation.value()) {
            status = FramePreparation::Deferred;
            discard_frame_requests();
        }
        failed.release();
        return Preparation::success(status);
    }

    RenderFrame Renderer::capture_frame() {
        RenderFrame frame;
        if(!m_frames->is_recording_frame())
            return frame;
        frame.serial = m_frames->get_current_frame_serial();
        frame.views.reserve(m_views.size());
        for(auto& [id, view] : m_views) {
            auto parameters = view.view;
            parameters.render_size = view.scene->get_render_target().get_size();
            const bool draw = parameters.visible || !view.scene->is_offscreen();
            frame.views.push_back({id, std::move(parameters), draw,
                std::exchange(view.pick_request, std::nullopt), std::move(view.lines)});
            view.lines = {};
        }
        if(m_capture_overlay)
            frame.overlays = m_capture_overlay();
        return frame;
    }

    void Renderer::set_ray_scene_sources(RaySceneBuilder::MeshReader read,
        RaySceneBuilder::MeshRevision revision, RaySceneBuilder::Images images) {
        m_ray_read = std::move(read);
        m_ray_revision = std::move(revision);
        m_ray_images = std::move(images);
        m_irradiance_builder.reset();
        m_irradiance_input.reset();
        m_irradiance_error.clear();
        auto support = check_path_trace_support();
        m_path_trace_support_error = support ? std::string{} : support.error().message;
        for(auto& [id, view] : m_views) {
            view.path_trace_builder.reset();
            view.path_trace_error.clear();
        }
    }

    const std::string& Renderer::path_trace_error(ViewId id) const {
        return get_view(id).path_trace_error;
    }

    SkinningPass::Statistics Renderer::get_skinning_statistics() const {
        return m_skinning ? m_skinning->get_statistics() : SkinningPass::Statistics{};
    }

    RealtimeIrradiancePass::Statistics Renderer::get_irradiance_statistics() const {
        return m_realtime_irradiance ? m_realtime_irradiance->statistics()
                                     : RealtimeIrradiancePass::Statistics{};
    }

    void Renderer::prepare_irradiance(RenderFrame& frame) {
        frame.irradiance.reset();
        const auto report = [&](std::string error) {
            if(error != m_irradiance_error && !error.empty())
                LOG_ERROR("实时探针 GI 准备失败：{}", error);
            m_irradiance_error = std::move(error);
        };
        std::vector<RenderIrradianceVolume> selected;
        if(frame.world && frame.has_raster_view())
            for(const auto& volume : frame.world->irradiance_volumes)
                if(volume.volume.realtime) {
                    if(auto valid = RealtimeIrradiancePass::validate_region(volume.volume);
                        !valid) {
                        report(valid.error());
                        return;
                    }
                    if(selected.size() == LightingData::MAX_IRRADIANCE_VOLUMES) {
                        report("实时 GI 最多支持 4 个区域，请禁用多余区域");
                        return;
                    }
                    selected.push_back(volume.volume);
                }
        if(selected.empty()) {
            m_irradiance_builder.reset();
            m_irradiance_input.reset();
            report({});
            return;
        }
        if(!frame.world->resolution.pending_asset_slots.empty())
            return;
        if(!m_irradiance_support_error.empty()) {
            report(m_irradiance_support_error);
            return;
        }
        if(!m_irradiance_builder) {
            m_irradiance_builder = std::make_unique<RaySceneBuilder>(512);
            m_irradiance_input = std::make_unique<RenderWorld>();
        }
        // 从同一份已解析世界筛选追踪几何，不再次提取 Scene，也不按视图重复构建。
        auto& input = *m_irradiance_input;
        input.scene_lifetime = frame.world->scene_lifetime;
        input.environment = frame.world->environment;
        input.environment.fog_enabled = false;
        input.environment.background = false;
        input.environment.background_color = {};
        input.environment.intensity = 1;
        input.environment_revision = frame.world->environment_revision;
        if(!input.environment.lighting)
            input.environment.asset = {};
        input.lights = frame.world->lights;
        input.render_items.clear();
        for(const auto& item : frame.world->render_items)
            if(item.contributes_to_gi && item.mesh)
                input.render_items.push_back(item);
        auto scene = m_irradiance_builder->update(input, m_ray_read, m_ray_revision, m_ray_images);
        report(scene ? std::string{} : scene.error());
        if(scene)
            frame.irradiance =
                RenderFrame::IrradianceInput{std::move(selected), std::move(scene).value()};
    }

    void Renderer::prepare_scene_views(RenderFrame& frame) {
        for(auto& input : frame.views) {
            auto& view = get_view(input.id);
            view.path_trace_error.clear();
            if(!input.draw || !input.parameters.path_trace_enabled || !frame.world) {
                view.path_trace_builder.reset();
                continue;
            }
            input.parameters.path_trace.reset();
            if(frame.world->environment.fog_enabled
                || std::ranges::any_of(frame.world->render_items,
                    [](const auto& item) { return item.mesh && item.mesh->is_skinned(); })
                || (frame.world->particles
                    && (!frame.world->particles->instances.empty()
                        || !frame.world->particles->gpu_emitters.empty()))) {
                view.path_trace_error = "路径追踪暂不支持蒙皮／雾／粒子";
                continue;
            }
            if(!m_path_trace_support_error.empty()) {
                view.path_trace_error = m_path_trace_support_error;
                continue;
            }
            if(!frame.world->resolution.pending_asset_slots.empty()) {
                view.path_trace_error = "场景资源尚未加载，路径追踪等待导入完成";
                continue;
            }
            if(!view.path_trace_builder)
                view.path_trace_builder = std::make_unique<RaySceneBuilder>();
            auto scene = view.path_trace_builder->update(
                *frame.world, m_ray_read, m_ray_revision, m_ray_images);
            if(scene)
                input.parameters.path_trace = std::move(scene).value();
            else
                view.path_trace_error = scene.error();
        }
        // 路径追踪准备失败时仍绘制光栅；据最终视图路径决定是否需要 GI 输入。
        prepare_irradiance(frame);
    }

    Result<void, GraphicsError> Renderer::render_frame(std::shared_ptr<const RenderWorld> world) {
        auto frame = capture_frame();
        frame.world = std::move(world);
        prepare_scene_views(frame);
        auto result = record_frame(std::move(frame));
        if(!result)
            prepare_shutdown();
        return result;
    }

    Result<void, GraphicsError> Renderer::record_frame(RenderFrame frame) {
        const bool diagnosed = m_diagnostics->is_enabled();
        RenderDiagnostics::Clock::time_point start{};
        if(diagnosed)
            start = RenderDiagnostics::Clock::now();
        if(!diagnosed)
            return draw_frame(frame);
        auto result = draw_frame(frame);
        if(result)
            m_diagnostics->record_frame(frame.serial,
                std::chrono::duration<double, std::milli>(RenderDiagnostics::Clock::now() - start)
                        .count()
                    + std::exchange(m_frame_preparation_ms, 0));
        return result;
    }

    Result<void, GraphicsError> Renderer::draw_frame(RenderFrame& frame) {
        const ScopeExit release_geometry([this] { m_geometry.clear_frame(); });
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        if(!m_frames->is_recording_frame() || frame.serial != m_frames->get_current_frame_serial())
            return Result<void, GraphicsError>::failure({"No matching prepared render frame"});
        PROFILE_SCOPE("render frame");
        if(!frame.has_visible_view()) {
            for(auto& [id, view] : m_views) {
                view.pick_result.reset();
                view.scene->skip_frame();
            }
            m_diagnostics->skip_frame();
            std::vector<QueueSemaphoreSubmit> waits;
            if(auto recorded = record_overlay(frame.overlays, waits); !recorded)
                return recorded;
            return m_presentation->end_frame(waits);
        }
        const RenderWorld empty_world;
        const auto& world = frame.world ? *frame.world : empty_world;
        std::vector<QueueSemaphoreSubmit> resource_waits;
        const bool needs_raster = frame.has_raster_view();
        if(needs_raster) {
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
            if(!m_skinning)
                m_skinning = std::make_shared<SkinningPass>(
                    m_render_context->get_device(), m_settings.render.max_frames_in_flight);
            if(auto skinned = m_skinning->prepare(world, *m_frames, m_geometry, resource_waits);
                !skinned)
                return skinned;
            m_frames->retain_current_frame_resource(m_skinning);
        }
        std::vector<ResolvedIrradianceVolume> irradiance;
        if(needs_raster && frame.irradiance) {
            if(!m_realtime_irradiance)
                m_realtime_irradiance =
                    std::make_unique<RealtimeIrradiancePass>(m_render_context->get_device());
            const auto& input = *frame.irradiance;
            auto updated = m_realtime_irradiance->update(*m_frames, world.scene_lifetime,
                input.scene, input.volumes, world.environment.realtime_gi_budget,
                m_diagnostics->is_enabled());
            if(!updated)
                return Result<void, GraphicsError>::failure(updated.error());
            irradiance = std::move(updated).value();
        } else if(m_realtime_irradiance) {
            m_realtime_irradiance->reset();
        }
        // 诊断查询覆盖第一个可见 View；其他 View 不重复开启同一组 GPU 查询。
        bool diagnosed = false;
        for(const auto& input : frame.views) {
            auto& view = get_view(input.id);
            if(!input.draw) {
                view.pick_result.reset();
                view.scene->skip_frame();
                continue;
            }
            auto* diagnostics = diagnosed ? nullptr : m_diagnostics.get();
            if(auto recorded = render_view(view, input, world, frame.world != nullptr,
                   resource_waits, diagnostics, irradiance);
                !recorded)
                return recorded;
            diagnosed = true;
        }
        if(auto recorded = record_overlay(frame.overlays, resource_waits); !recorded)
            return recorded;

        return m_presentation->end_frame(resource_waits, m_diagnostics.get());
    }

    Result<void, GraphicsError> Renderer::render_view(ViewState& view,
        const RenderFrame::View& input, const RenderWorld& world, const bool has_scene,
        std::vector<QueueSemaphoreSubmit>& waits, RenderDiagnostics* diagnostics,
        std::span<const ResolvedIrradianceVolume> irradiance) {
        const auto& frame_view = input.parameters;
        const RenderSubmission submission{world,
            has_scene ? view.camera.resolve(world, frame_view) : std::nullopt,
            frame_view.path_trace, frame_view.path_trace_settings, irradiance,
            frame_view.irradiance_debug};
        if(auto prepared = view.scene->prepare_post_process(world.post_process); !prepared)
            return prepared;
        const auto& pick_request = input.pick;
        if(pick_request && has_scene && frame_view.visible
            && pick_request->scene_lifetime == world.scene_lifetime
            && pick_request->image_resolution == frame_view.render_size)
            view.pick_result = ViewportPickResult{world.scene_lifetime,
                pick_render_submission(submission, pick_request->pixel, frame_view.render_size)};
        const LineDrawList empty_lines;
        const auto& lines = frame_view.visible ? input.lines : empty_lines;
        auto resource_waits =
            view.scene->render(*m_frames, submission, m_geometry, lines, diagnostics);
        if(!resource_waits)
            return Result<void, GraphicsError>::failure(resource_waits.error());
        for(const auto& wait : resource_waits.value())
            merge_semaphore_wait(waits, wait);
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> Renderer::enable_offscreen_rendering(
        const Math::Vec2u initial_size) {
        if(m_render_thread && !m_render_thread->is_current())
            return m_render_thread->invoke(
                [this, initial_size] { return enable_offscreen_rendering(initial_size); });
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        if(m_frames->is_frame_active())
            return Result<void, GraphicsError>::failure(
                {"Target configuration requires a frame boundary"});
        return get_view(DEFAULT_VIEW).scene->configure_offscreen(*m_render_resources, initial_size);
    }

    Result<MaterialRenderer::ReloadReport, GraphicsError> Renderer::reload_material_shaders(
        MaterialShaders shaders) {
        if(m_render_thread && !m_render_thread->is_current())
            return m_render_thread->invoke([this, shaders = std::move(shaders)]() mutable {
                return reload_material_shaders(std::move(shaders));
            });
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
                if(auto invalidated = invalidate_material_shaders(failed); !invalidated)
                    return Result<MaterialRenderer::ReloadReport, GraphicsError>::failure(
                        invalidated.error());
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

    Result<void, GraphicsError> Renderer::invalidate_material_shaders(
        const std::span<const std::string> programs) {
        if(m_render_thread && !m_render_thread->is_current())
            return m_render_thread->invoke(
                [this, programs = std::vector<std::string>(programs.begin(), programs.end())] {
                    return invalidate_material_shaders(programs);
                });
        if(m_shutdown_prepared || m_frames->is_frame_active())
            return Result<void, GraphicsError>::failure(
                {"Shader invalidation requires a live renderer frame boundary"});
        m_programs->invalidate_builtin(programs);
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> Renderer::MaterialUpdate::publish() && {
        if(m_owner->m_render_thread && !m_owner->m_render_thread->is_current())
            return m_owner->m_render_thread->invoke(
                [update = std::move(*this)]() mutable { return std::move(update).publish(); });
        if(m_owner->m_shutdown_prepared || m_owner->m_frames->is_frame_active())
            return Result<void, GraphicsError>::failure(
                {"Material publication requires a live renderer frame boundary"});
        for(auto& update : m_views)
            std::move(update).publish();
        m_views.clear();
        return Result<void, GraphicsError>::success();
    }

    Result<Renderer::MaterialUpdate, GraphicsError> Renderer::prepare_material_update(
        const AssetHandle handle, const std::shared_ptr<const Material>& snapshot,
        const std::shared_ptr<const ShaderProgramArtifact>& program_source) {
        if(m_render_thread && !m_render_thread->is_current())
            return m_render_thread->invoke([this, handle, snapshot, program_source] {
                return prepare_material_update(handle, snapshot, program_source);
            });
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
        update.m_owner = this;
        update.m_views = std::move(candidates).value();
        return Preparation::success(std::move(update));
    }

    Result<void, GraphicsError> Renderer::maintain_assets(AssetRegistry::Removals removals) {
        if(m_render_thread && !m_render_context->get_device().is_resource_owner_thread())
            return m_render_thread->invoke([this, removals = std::move(removals)]() mutable {
                return maintain_assets(std::move(removals));
            });
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        m_programs->remove_assets(removals);
        for(auto& [id, view] : m_views)
            view.scene->remove_assets(removals);
        collect_resources();
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> Renderer::apply_pending_quality() {
        if(!m_pending_quality)
            return Result<void, GraphicsError>::success();
        const auto quality = std::exchange(m_pending_quality, std::nullopt).value();
        std::vector<std::shared_ptr<SceneRenderer::RenderState>> candidates;
        candidates.reserve(m_views.size());
        for(auto& [id, view] : m_views) {
            auto candidate = view.scene->prepare_quality(
                *m_render_resources, m_render_context->get_swapchain(), quality);
            if(!candidate) {
                m_quality_error = candidate.error().message;
                if(candidate.error().is_device_lost())
                    return Result<void, GraphicsError>::failure(candidate.error());
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
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> Renderer::warmup_scene(std::shared_ptr<const RenderWorld> world) {
        if(m_render_thread && !m_render_thread->is_current())
            return m_render_thread->invoke(
                [this, world = std::move(world)] { return warmup_scene(world); });
        if(m_shutdown_prepared || !world)
            return Result<void, GraphicsError>::failure(
                {"Scene warmup requires a live renderer and snapshot"});
        if(auto applied = apply_pending_quality(); !applied)
            return applied;
        std::vector<MaterialRenderer*> materials;
        materials.reserve(m_views.size());
        for(auto& [id, view] : m_views)
            materials.push_back(&view.scene->get_material_renderer());
        if(auto prepared = MaterialRenderer::prepare_programs(materials, *world); !prepared)
            return prepared;
        for(auto& [id, view] : m_views) {
            if(auto prepared = view.scene->warmup_scene(*world); !prepared)
                return prepared;
        }
        return Result<void, GraphicsError>::success();
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
        m_capture_overlay = std::move(overlay.capture);
        m_shutdown_overlay = std::move(overlay.shutdown);
        m_presentation->set_overlay({std::move(overlay.release), std::move(overlay.rebuild)});
    }

    Result<void, GraphicsError> Renderer::record_overlay(
        const OverlayDrawList& draws, std::vector<QueueSemaphoreSubmit>& waits) {
        OverlayRecordContext context(*m_frames, waits);
        for(const auto& draw : draws)
            if(auto recorded = draw->record(context); !recorded)
                return recorded;
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> Renderer::prepare_overlay_resources(
        const std::function<Result<void, GraphicsError>()>& prepare) {
        if(m_render_thread && !m_render_thread->is_current())
            return m_render_thread->invoke(
                [this, prepare] { return prepare_overlay_resources(prepare); });
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        if(!prepare)
            return Result<void, GraphicsError>::failure({"Overlay preparation is empty"});
        return prepare();
    }

    Result<void, GraphicsError> Renderer::set_render_view(RenderView view, const ViewId id) {
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        const auto found = m_views.find(id);
        if(found == m_views.end())
            return Result<void, GraphicsError>::failure({"Unknown render view"});
        if(view.visible && view.render_size.x > 0 && view.render_size.y > 0) {
            // 尺寸未变时只更新 CPU 视图状态，避免每帧同步唤醒 Worker。
            if(m_render_thread && !m_render_thread->is_current()
                && found->second.scene->is_offscreen()
                && found->second.scene->get_render_target().get_size() != view.render_size)
                return m_render_thread->invoke([this, view = std::move(view), id]() mutable {
                    return set_render_view(std::move(view), id);
                });
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
        view.pick_request = RenderFrame::Pick{pixel, image_resolution, scene_lifetime};
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
    }

    void Renderer::prepare_shutdown() noexcept {
        if(std::exchange(m_shutdown_prepared, true))
            return;
        m_render_context->get_device().wait_idle_for_shutdown();
        discard_frame_requests();
        if(auto shutdown = std::exchange(m_shutdown_overlay, {}))
            shutdown();
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        prepare_shutdown();

        m_presentation.reset();
        m_diagnostics.reset();
        m_skinning.reset();
        m_realtime_irradiance.reset();
        m_irradiance_input.reset();
        m_irradiance_builder.reset();
        m_frames.reset();
        m_views.clear();
        m_render_resources.reset();
        m_render_context.reset();
    }
}
