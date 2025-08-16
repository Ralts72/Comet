#include "engine.h"
#include "common/log_system/log_system.h"
#include "graphics/image.h"

namespace Comet {

    Engine::Engine() {
        LOG_INFO("init glfw");
        if(!glfwInit()) {
            LOG_ERROR("Failed to init glfw.");
            return;
        }

        LOG_INFO("init window");
        m_window = std::make_unique<Window>("Comet Engine", 1024, 720);
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
            m_window->poll_events();
            m_window->swap_buffers();
        }
    }
}
