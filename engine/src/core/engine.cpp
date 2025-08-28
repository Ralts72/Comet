#include "engine.h"
#include "common/logger.h"
#include "common/profiler.h"

namespace Comet {
    Engine::Engine(const Settings& settings) {
        PROFILE_SCOPE("Engine::Constructor");
        LOG_INFO("init timer");
        m_timer = std::make_unique<Timer>();

        LOG_INFO("init glfw");
        if(!glfwInit()) {
            LOG_ERROR("Failed to init glfw.");
            return;
        }

        LOG_INFO("init window");
        m_window = std::make_unique<Window>(settings.title, settings.width, settings.height);

        LOG_INFO("init renderer");
        m_renderer = std::make_unique<Renderer>(*m_window);
    }

    Engine::~Engine() {
        LOG_INFO("shutting down engine...");
        m_renderer.reset();
        m_window.reset();
        PROFILE_RESULTS();
    }

    void Engine::on_update() {
        LOG_INFO("running engine...");
        while(!m_window->should_close()) {
            PROFILE_SCOPE("Engine::Frame");
            m_window->poll_events();
            m_timer->tick();
            const auto update_context = m_timer->get_update_context();
            m_renderer->on_update(update_context.deltaTime);
            m_renderer->on_render();

            for(auto& callback: m_update_callbacks) {
                callback(update_context);
            }

            m_window->swap_buffers();
        }
    }
}
