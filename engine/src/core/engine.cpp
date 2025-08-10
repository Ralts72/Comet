#include "engine.h"
#include "common/log_system/log_system.h"

namespace Comet {
    // template<>
    // std::unique_ptr<Comet::Engine> Comet::Singleton<Comet::Engine, true>::m_instance = nullptr;

    Engine::Engine() {
        LOG_INFO("init glfw");
        if(!glfwInit()) {
            LOG_ERROR("Failed to init glfw.");
            return;
        }

        LOG_INFO("init window");
        m_window = std::make_unique<Window>("Comet Engine", 1024, 720);

        LOG_INFO("init graphics system");
        m_context = std::make_unique<Context>(*m_window);
        //
        // LOG_INFO("init storage system");
        // m_storage_manager = std::make_unique<StorageManager>("Ralts", "Comet");
    }

    Engine::~Engine() {
        LOG_INFO("shutting down engine...");
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
