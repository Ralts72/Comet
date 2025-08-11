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

        LOG_INFO("create device");
        m_device = std::make_unique<Device>(m_context.get(), 1, 1);

        LOG_INFO("create swapchain");
        m_swapchain = std::make_unique<Swapchain>(m_context.get(), m_device.get());
        m_swapchain->recreate();
    }

    Engine::~Engine() {
        LOG_INFO("shutting down engine...");
        m_swapchain.reset();
        m_device.reset();
        m_context.reset();
        m_window.reset();
        glfwTerminate();
    }

    void Engine::on_update() {
        LOG_INFO("running engine...");
        while(!m_window->should_close()) {
            m_window->poll_events();
            m_window->swap_buffers();
        }
    }
}
