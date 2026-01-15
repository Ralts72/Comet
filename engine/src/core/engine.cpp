#include "engine.h"
#include "common/logger.h"
#include "common/profiler.h"
#include "common/config.h"

namespace Comet {
    Engine::Engine() {
        PROFILE_SCOPE("Engine::Constructor");
        LOG_INFO("init timer");
        m_timer = std::make_unique<Timer>();

        LOG_INFO("init glfw");
        if(!glfwInit()) {
            LOG_ERROR("Failed to init glfw.");
            return;
        }

        int width = Config::get<int>("window.width", 1280);
        int height = Config::get<int>("window.height", 720);
        auto title = Config::get<std::string>("window.title", "Comet Engine");

        LOG_INFO("init window");
        m_window = std::make_unique<Window>(title, width, height);

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

        static bool show_fps = Config::get<bool>("debug.show_fps", true);
        static float fps_display_timer = 0.0f;

        while(!m_window->should_close()) {
            PROFILE_SCOPE("Engine::Frame");
            m_window->poll_events();
            m_timer->tick();
            const auto update_context = m_timer->get_update_context();

            // 显示FPS（每秒更新一次）
            if(show_fps) {
                fps_display_timer += update_context.deltaTime;
                if(fps_display_timer >= 1.0f) {
                    LOG_INFO("FPS: {:.1f} | Frame Time: {:.2f}ms", update_context.fps, 1000.0f / update_context.fps);
                    fps_display_timer = 0.0f;
                }
            }

            m_renderer->on_update(update_context.deltaTime);
            m_renderer->on_render();

            for(auto& callback: m_update_callbacks) {
                callback(update_context);
            }

            m_window->swap_buffers();
        }
    }
}
