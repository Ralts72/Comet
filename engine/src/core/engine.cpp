#include "engine.h"
#include "common/logger.h"
#include "common/profiler.h"

namespace Comet {
    Engine::Engine(Config::Runtime config)
        : m_config(std::move(config)) {
        PROFILE_SCOPE("Engine::Constructor");
        LOG_INFO("init timer");
        m_timer = std::make_unique<Timer>();

        LOG_INFO("init glfw");
        if(!glfwInit()) {
            LOG_ERROR("Failed to init glfw.");
            return;
        }

        LOG_INFO("init window");
        m_window = std::make_unique<Window>(m_config.window);

        LOG_INFO("init renderer");
        m_renderer = std::make_unique<Renderer>(*m_window, m_config);
    }

    Engine::~Engine() {
        LOG_INFO("shutting down engine...");
        m_renderer.reset();
        m_window.reset();
        PROFILE_RESULTS();
    }

    void Engine::on_update() const {
        LOG_INFO("running engine...");

        while(!m_window->should_close()) {
            PROFILE_SCOPE("Engine::Frame");
            m_window->poll_events();
            m_timer->tick();
            const auto update_context = m_timer->get_update_context();

            for(auto& callback: m_update_callbacks) {
                callback(update_context);
            }

            m_renderer->on_update(update_context.deltaTime);
            m_renderer->on_render();

            m_window->swap_buffers();
        }
    }
}
