#include "engine.h"
#include "core/logger/logger.h"

namespace Comet {

    Engine::Engine() {
        LOG_INFO("init timer");
        m_timer = std::make_unique<Timer>();

        LOG_INFO("init glfw");
        if(!glfwInit()) {
            LOG_ERROR("Failed to init glfw.");
            return;
        }

        LOG_INFO("init window");
        m_window = std::make_unique<Window>("Comet Engine", 1024, 720);

        LOG_INFO("init renderer");
        m_renderer = std::make_unique<Renderer>(*m_window);
    }

    Engine::~Engine() {
        LOG_INFO("shutting down engine...");
        m_renderer.reset();
        m_window.reset();
    }

    void Engine::on_update() {
        LOG_INFO("running engine...");
        while(!m_window->should_close()) {
            m_timer->tick();
            const auto update_context = m_timer->get_update_context();
            m_renderer->on_render(update_context.deltaTime);

            for (auto& callback : m_update_callbacks) {
                callback(update_context);
            }

            m_window->poll_events();
            m_window->swap_buffers();
        }
    }
}
