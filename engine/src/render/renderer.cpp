#include "renderer.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "render/scene/render_types.h"

#include <utility>

namespace Comet {
    Renderer::Renderer(
        const Window& window, const Config& config, const AssetRegistry& asset_registry)
        : m_scene_resolver(asset_registry) {
        PROFILE_SCOPE("Renderer::Constructor");

        // Create render context
        m_render_context =
            std::make_unique<RenderContext>(window, config.vulkan, config.render);

        // Create resource manager
        LOG_INFO("create resource manager");
        m_resource_manager =
            std::make_unique<ResourceManager>(m_render_context->get_device());

        // Create scene renderer
        LOG_INFO("create scene renderer");
        m_scene_renderer = std::make_unique<SceneRenderer>(
            *m_render_context, config.vulkan, config.render);

        // Setup render pass (moved to SceneRenderer)
        m_scene_renderer->setup_render_pass();

        m_scene_renderer->setup_pipeline(*m_resource_manager);
    }

    void Renderer::on_render(const RenderScene& render_scene) {
        PROFILE_SCOPE("render frame");
        m_resource_manager->collect_completed_uploads();

        // Begin frame (acquires image and begins command buffer)
        if(!m_scene_renderer->begin_frame()) {
            return;
        }
        RenderView frame_view = m_render_view;
        frame_view.render_size = m_scene_renderer->get_render_target().get_size();
        const RenderSubmission submission =
            m_scene_resolver.resolve(render_scene, frame_view);
        const auto resource_waits = m_scene_renderer->render_scene_pass(submission);

        if(m_on_imgui_render) {
            m_on_imgui_render(m_scene_renderer->get_current_command_buffer());
        }

        // End frame (submits and presents)
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

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        m_render_context->wait_idle();

        m_scene_renderer.reset();
        m_resource_manager.reset();
        m_render_context.reset();
    }
}
