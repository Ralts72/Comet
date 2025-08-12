#include "engine.h"
#include "common/log_system/log_system.h"

namespace Comet {
    // template<>
    // std::unique_ptr<Comet::Engine> Comet::Singleton<Comet::Engine, true>::m_instance = nullptr;

    static VkSettings s_vk_settings = {
        .surface_format = vk::Format::eB8G8R8A8Unorm,
        .color_space = vk::ColorSpaceKHR::eSrgbNonlinear,
        .depth_format = vk::Format::eD32Sfloat,
        .present_mode = vk::PresentModeKHR::eImmediate,
        .swapchain_image_count = 3
    };

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
        m_device = std::make_shared<Device>(m_context.get(), 1, 1, s_vk_settings);

        LOG_INFO("create swapchain");
        m_swapchain = std::make_shared<Swapchain>(m_context.get(), m_device.get());
        m_swapchain->recreate();

        LOG_INFO("create renderpass");
        m_render_pass = std::make_shared<RenderPass>(m_device.get());
    }

    Engine::~Engine() {
        LOG_INFO("shutting down engine...");
        m_render_pass.reset();
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
