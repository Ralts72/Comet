#include "renderer.h"
#include "common/scope_exit.h"
#include "config/config.h"
#include "render/render_context.h"
#include "render/render_diagnostics.h"
#include "render/material/material_programs.h"
#include "render/resource/render_resources.h"
#include "render/scene/scene_renderer.h"
#include "core/window.h"
#include "graphics/device.h"
#include "graphics/convert.h"
#include "render/render_target.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"

#include <utility>

namespace Comet {
    Result<std::unique_ptr<Renderer>, GraphicsError> Renderer::create(
        const Window& window, const Config& config, const AssetRegistry& asset_registry) {
        using Creation = Result<std::unique_ptr<Renderer>, GraphicsError>;
        if(config.render.max_frames_in_flight == 0)
            return Creation::failure({"Renderer requires at least one frame slot"});
        auto context = RenderContext::create(window, config.vulkan, config.render);
        if(!context)
            return Creation::failure(context.error());
        auto& device = context.value()->get_device();
        auto resources = std::make_unique<RenderResources>(device);
        auto frames = std::make_unique<FrameScheduler>(device, config.render.max_frames_in_flight);
        auto programs = std::make_unique<MaterialPrograms>(asset_registry);
        auto& swapchain = context.value()->get_swapchain();
        frames->initialize_swapchain_images(static_cast<uint32_t>(swapchain.get_images().size()));
        auto scene =
            std::make_unique<SceneRenderer>(device, *programs, config.vulkan, config.render);
        auto configured = Result<void, GraphicsError>::success();
        if(config.render.scene_output == Config::Render::SceneOutput::Offscreen)
            configured = scene->configure_offscreen(
                *resources, {swapchain.get_width(), swapchain.get_height()});
        else
            configured = scene->configure_presentation(*resources, swapchain);
        if(!configured)
            return Creation::failure(configured.error());
        auto renderer =
            std::unique_ptr<Renderer>(new Renderer(std::move(context).value(), std::move(resources),
                std::move(frames), std::move(programs), std::move(scene), asset_registry));
        if(auto enabled =
                renderer->m_diagnostics->set_enabled(config.diagnostics.enable_render_diagnostics);
            !enabled)
            return Creation::failure({enabled.error()});
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
                        m_render_context->get_swapchain(), compatibility);
                }});
    }

    Renderer::OffscreenFrame Renderer::get_offscreen_frame() const {
        if(!m_frames->is_frame_active() || !m_scene_renderer->is_offscreen())
            LOG_FATAL("Offscreen frame requires an active offscreen renderer frame");
        const auto slot = m_frames->get_current_frame_slot_index();
        return {slot, m_scene_renderer->get_render_target().get_size(),
            m_scene_renderer->get_offscreen_color_view(slot)};
    }

    std::vector<std::shared_ptr<const MaterialLayout>> Renderer::get_material_layouts() const {
        return m_scene_renderer->get_material_layouts();
    }

    Result<bool, GraphicsError> Renderer::prepare_frame() {
        if(m_shutdown_prepared)
            return Result<bool, GraphicsError>::failure({"Renderer is shutting down"});
        ScopeExit failed([this] { prepare_shutdown(); });
        if(m_frames->is_frame_active())
            return Result<bool, GraphicsError>::failure({"A render frame is already active"});
        PROFILE_SCOPE("prepare frame");
        m_render_resources->collect_completed_uploads();
        m_programs->collect_removed();
        m_scene_renderer->collect_removed_assets(m_asset_registry);

        auto preparation = m_presentation->begin_frame();
        if(preparation) {
            if(auto collected = m_diagnostics->collect_completed(); !collected)
                return Result<bool, GraphicsError>::failure(collected.error());
            m_diagnostics->poll_memory();
        }
        if(!preparation || !preparation.value()) {
            m_viewport_pick_request.reset();
            m_line_draw_list.clear();
        }
        if(preparation)
            failed.release();
        return preparation;
    }

    Result<void, GraphicsError> Renderer::render_frame(const RenderScene& render_scene) {
        if(m_shutdown_prepared)
            return Result<void, GraphicsError>::failure({"Renderer is shutting down"});
        ScopeExit failed([this] { prepare_shutdown(); });
        if(!m_frames->is_recording_frame())
            return Result<void, GraphicsError>::failure({"No prepared render frame"});
        PROFILE_SCOPE("render frame");
        if(!m_render_view.visible && m_scene_renderer->is_offscreen()) {
            m_viewport_pick_request.reset();
            m_line_draw_list.clear();
            m_scene_renderer->skip_frame();
            m_diagnostics->skip_frame();
            if(m_render_overlay)
                m_render_overlay(m_frames->get_current_command_buffer());
            auto submitted = m_presentation->end_frame({});
            if(submitted)
                failed.release();
            return submitted;
        }
        RenderView frame_view = m_render_view;
        frame_view.render_size = m_scene_renderer->get_render_target().get_size();
        const RenderSubmission submission = m_scene_resolver.resolve(render_scene, frame_view);
        if(auto programs = m_scene_renderer->prepare_material_programs(submission); !programs)
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
        const auto resource_waits =
            m_scene_renderer->render(*m_frames, submission, m_line_draw_list, m_diagnostics.get());
        m_line_draw_list.clear();

        if(!resource_waits) {
            return Result<void, GraphicsError>::failure(resource_waits.error());
        }

        if(m_render_overlay) {
            m_render_overlay(m_frames->get_current_command_buffer());
        }

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

    void Renderer::request_swapchain_recreation() {
        m_presentation->request_recreation();
    }

    void Renderer::wait_idle() {
        // 关闭准备已等待设备；失败遗留的未提交帧不能再按正常帧等待。
        if(m_shutdown_prepared)
            return;
        m_frames->wait_for_all_slots();
        m_render_context->get_device().get_present_queue(0).wait_idle();
    }

    void Renderer::set_swapchain_resource_callbacks(std::function<void()> release,
        std::function<Result<void, GraphicsError>(const SwapchainCompatibility&)> rebuild) {
        m_presentation->set_overlay({std::move(release), std::move(rebuild)});
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

    void Renderer::set_overlay_renderer(OverlayRenderCallback render) {
        m_render_overlay = std::move(render);
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

    void Renderer::prepare_shutdown() noexcept {
        if(std::exchange(m_shutdown_prepared, true))
            return;
        m_render_context->get_device().wait_idle_for_shutdown();
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
