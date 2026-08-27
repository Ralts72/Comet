#include "engine.h"
#include "asset/registry.h"
#include "common/logger.h"
#include "common/profiler.h"
#include "render/scene_extractor.h"
#include "scene/scene.h"

namespace Comet {
    Engine::Engine(Config config)
        : m_config(std::move(config)) {
        PROFILE_SCOPE("Engine::Constructor");
        LOG_INFO("init timer");
        m_timer = std::make_unique<Timer>();
        m_asset_registry = std::make_unique<AssetRegistry>();

        LOG_INFO("init glfw");
        if(!glfwInit()) {
            LOG_ERROR("Failed to init glfw.");
            return;
        }

        LOG_INFO("init window");
        m_window = std::make_unique<Window>(m_config.window);

        LOG_INFO("init renderer");
        m_renderer = std::make_unique<Renderer>(
            *m_window, m_config, *m_asset_registry);
    }

    Engine::~Engine() {
        LOG_INFO("shutting down engine...");
        m_asset_registry->clear();
        m_renderer.reset();
        m_asset_registry.reset();
        m_scene.reset();
        m_window.reset();
    }

    void Engine::set_scene(std::unique_ptr<Scene> scene) {
        m_scene = std::move(scene);
    }

    std::unique_ptr<Scene> Engine::replace_scene(
        std::unique_ptr<Scene> scene) noexcept {
        m_scene.swap(scene);
        return scene;
    }

    void Engine::on_update() const {
        LOG_INFO("running engine...");

        while(!m_window->should_close()) {
            PROFILE_SCOPE("Engine::Frame");
            m_window->poll_events();
            if(m_window->should_close()) {
                break;
            }

            const auto framebuffer_size = m_window->get_framebuffer_size();
            if(framebuffer_size.x == 0 || framebuffer_size.y == 0) {
                m_window->wait_events();
                m_timer->tick();
                continue;
            }

            m_timer->tick();
            const auto update_context = m_timer->get_update_context();

            for(auto& callback: m_update_callbacks) {
                callback(update_context);
            }

            RenderScene render_scene;
            if(m_scene) {
                render_scene = SceneExtractor::extract(*m_scene);
            }

            m_renderer->on_render(render_scene);

            m_window->swap_buffers();
        }
    }
}
