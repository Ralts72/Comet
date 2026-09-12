#include "renderer.h"
#include "config/config.h"
#include "render/render_context.h"
#include "render/resource/resource_manager.h"
#include "render/scene/scene_renderer.h"
#include "core/window.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"

#include <utility>

namespace Comet {
    Renderer::Renderer(
        const Window& window, const Config& config, const AssetRegistry& asset_registry)
        : m_scene_resolver(asset_registry) {
        PROFILE_SCOPE("Renderer::Constructor");

        m_render_context =
            std::make_unique<RenderContext>(window, config.vulkan, config.render);

        LOG_INFO("create resource manager");
        m_resource_manager =
            std::make_unique<ResourceManager>(m_render_context->get_device());

        LOG_INFO("create scene renderer");
        m_scene_renderer = std::make_unique<SceneRenderer>(
            *m_render_context, config.vulkan, config.render);

        m_scene_renderer->setup_render_pass();

        m_scene_renderer->setup_pipeline(*m_resource_manager);
    }

    bool Renderer::prepare_frame() {
        PROFILE_SCOPE("prepare frame");
        m_resource_manager->collect_completed_uploads();

        if(!m_scene_renderer->begin_frame()) {
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
        const RenderSubmission submission =
            m_scene_resolver.resolve(render_scene, frame_view);
        const auto pick_request = std::exchange(m_viewport_pick_request, std::nullopt);
        if(pick_request && frame_view.visible
            && pick_request->image_resolution == frame_view.render_size
            && m_viewport_pick_callback) {
            m_viewport_pick_callback(pick_render_submission(
                submission, pick_request->pixel, frame_view.render_size));
        }
        if(!frame_view.visible) {
            m_line_draw_list.clear();
        }
        const auto resource_waits =
            m_scene_renderer->render_scene_pass(submission, m_line_draw_list);
        m_line_draw_list.clear();

        if(m_render_overlay) {
            m_render_overlay(m_scene_renderer->get_current_command_buffer());
        }

        m_scene_renderer->end_frame(resource_waits);
    }

    void Renderer::enable_offscreen_rendering(const Math::Vec2u initial_size) {
        m_render_context->wait_idle();
        m_scene_renderer->setup_offscreen_render_pass(initial_size);
        m_scene_renderer->setup_pipeline(*m_resource_manager);
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
        m_render_context->wait_idle();

        m_scene_renderer.reset();
        m_resource_manager.reset();
        m_render_context.reset();
    }
}
