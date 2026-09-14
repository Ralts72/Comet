#include "renderer.h"
#include "config/config.h"
#include "render/render_context.h"
#include "render/resource/resource_manager.h"
#include "render/scene/scene_renderer.h"
#include "core/window.h"
#include "graphics/device.h"
#include "graphics/convert.h"
#include "render/render_target.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"

#include <utility>
#include <stdexcept>

namespace Comet {
    Renderer::Renderer(
        const Window& window, const Config& config, const AssetRegistry& asset_registry)
        : m_scene_resolver(asset_registry) {
        PROFILE_SCOPE("Renderer::Constructor");

        m_render_context = std::make_unique<RenderContext>(window, config.vulkan, config.render);

        LOG_INFO("create resource manager");
        m_resource_manager = std::make_unique<ResourceManager>(m_render_context->get_device());

        LOG_INFO("create scene renderer");
        m_frames = std::make_unique<FrameScheduler>(
            m_render_context->get_device(), config.render.max_frames_in_flight);
        auto& swapchain = m_render_context->get_swapchain();
        m_frames->initialize_swapchain_images(static_cast<uint32_t>(swapchain.get_images().size()));
        m_scene_renderer = std::make_unique<SceneRenderer>(m_render_context->get_device(),
            Graphics::vk_to_format(
                swapchain.get_active_generation()->get_config().surface_format.format),
            config.vulkan, config.render);
        if(auto result = m_scene_renderer->configure_presentation(*m_resource_manager, swapchain);
            !result)
            throw std::runtime_error("Cannot initialize scene target: " + result.error().message);
        m_presentation = std::make_unique<Presentation>(*m_render_context, *m_frames,
            Presentation::Dependent{[this] { m_scene_renderer->release_presentation_target(); },
                [this](const SwapchainCompatibility& compatibility) {
                    return m_scene_renderer->rebuild_presentation_target(
                        m_render_context->get_swapchain(), compatibility);
                }});
    }

    bool Renderer::prepare_frame() {
        PROFILE_SCOPE("prepare frame");
        m_resource_manager->collect_completed_uploads();

        if(!m_presentation->begin_frame()) {
            m_viewport_pick_request.reset();
            m_line_draw_list.clear();
            return false;
        }
        if(m_prepare_overlay) {
            m_prepare_overlay();
        }
        return true;
    }

    void Renderer::render_frame(const RenderScene& render_scene) {
        PROFILE_SCOPE("render frame");
        RenderView frame_view = m_render_view;
        frame_view.render_size = m_scene_renderer->get_render_target().get_size();
        const RenderSubmission submission = m_scene_resolver.resolve(render_scene, frame_view);
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
            m_scene_renderer->render_scene_pass(*m_frames, submission, m_line_draw_list);
        m_line_draw_list.clear();

        if(m_render_overlay) {
            m_render_overlay(m_frames->get_current_command_buffer());
        }

        m_presentation->end_frame(resource_waits);
    }

    Result<void, GraphicsError> Renderer::enable_offscreen_rendering(
        const Math::Vec2u initial_size) {
        if(m_frames->is_frame_active())
            return Result<void, GraphicsError>::failure(
                {"Target configuration requires a frame boundary"});
        return m_scene_renderer->configure_offscreen(*m_resource_manager, initial_size);
    }

    Result<MaterialRenderer::ReloadReport, GraphicsError> Renderer::reload_material_shaders(
        MaterialRenderer::ShaderCode shaders) {
        if(m_frames->is_frame_active())
            return Result<MaterialRenderer::ReloadReport, GraphicsError>::failure(
                {"Shader publication requires a frame boundary"});
        return m_scene_renderer->reload_material_shaders(std::move(shaders));
    }

    bool Renderer::recreate_swapchain() {
        return m_presentation->recreate_swapchain();
    }

    void Renderer::wait_idle() {
        m_frames->wait_for_all_slots();
        m_render_context->get_device().get_present_queue(0).wait_idle();
    }

    void Renderer::set_swapchain_resource_callbacks(std::function<void()> release,
        std::function<Result<void, GraphicsError>(const SwapchainCompatibility&)> rebuild) {
        m_presentation->set_overlay({std::move(release), std::move(rebuild)});
    }

    void Renderer::set_render_view(RenderView view) {
        m_render_view = std::move(view);
        if(m_render_view.visible && m_render_view.render_size.x > 0
            && m_render_view.render_size.y > 0) {
            m_scene_renderer->resize_offscreen_target(m_render_view.render_size);
        }
    }

    void Renderer::set_overlay_callbacks(
        OverlayPrepareCallback prepare, OverlayRenderCallback render) {
        m_prepare_overlay = std::move(prepare);
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
        m_line_draw_list.append(draw_list);
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        m_render_context->get_device().wait_idle_for_shutdown();

        m_presentation.reset();
        m_frames.reset();
        m_scene_renderer.reset();
        m_resource_manager.reset();
        m_render_context.reset();
    }
}
