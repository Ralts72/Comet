#include "engine.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "render/scene/scene_extractor.h"
#include "scene/scene.h"
#include "runtime/scene_runtime.h"

namespace Comet {
    Engine::Engine(const Config& config) {
        PROFILE_SCOPE("Engine::Constructor");
        LOG_INFO("init timer");
        m_timer = std::make_unique<Timer>();
        m_scene_runtime = std::make_unique<SceneRuntime>();
        LOG_INFO("init task scheduler");
        m_task_scheduler = std::make_unique<TaskScheduler>();
        m_asset_registry = std::make_unique<AssetRegistry>();

        LOG_INFO("init window");
        m_window = std::make_unique<Window>(config.window);

        LOG_INFO("init renderer");
        m_renderer = std::make_unique<Renderer>(*m_window, config, *m_asset_registry);
    }

    Engine::~Engine() {
        LOG_INFO("shutting down engine...");
        m_scene_runtime.reset();
        m_task_scheduler->wait_idle();
        m_renderer->get_render_context().wait_idle();
        m_asset_registry->clear();
        m_renderer.reset();
        m_asset_registry.reset();
        m_scene.reset();
        m_window.reset();
        m_task_scheduler.reset();
    }

    void Engine::set_scene(std::unique_ptr<Scene> scene) {
        m_scene_runtime->stop();
        m_scene = std::move(scene);
    }

    std::unique_ptr<Scene> Engine::replace_scene(std::unique_ptr<Scene> scene) {
        m_scene_runtime->stop();
        m_scene.swap(scene);
        return scene;
    }

    void Engine::on_update() {
        LOG_INFO("running engine...");

        while(!m_window->should_close()) {
            PROFILE_SCOPE("Engine::Frame");
            const bool measure =
                m_renderer->get_scene_renderer().get_diagnostics().is_enabled();
            const auto now = [measure] {
                return measure ? std::chrono::steady_clock::now()
                               : std::chrono::steady_clock::time_point{};
            };
            const auto frame_start = now();
            m_window->poll_events();
            if(m_window->should_close()) {
                break;
            }

            const auto framebuffer_size = m_window->get_framebuffer_size();
            if(framebuffer_size.x == 0 || framebuffer_size.y == 0) {
                m_frame_timing.reset();
                m_scene_runtime->discard_input();
                m_window->wait_events();
                m_timer->tick();
                continue;
            }

            const auto events_end = now();
            m_timer->tick();
            const auto update_context = m_timer->get_update_context();

            for(auto& callback : m_update_callbacks) {
                callback(update_context);
            }

            const auto update_end = now();
            const bool prepared = m_renderer->prepare_frame();
            const auto prepare_end = now();
            m_scene_runtime->advance(update_context.delta_time, get_input_frame());
            const auto simulation_end = now();
            if(prepared) {
                RenderScene render_scene;
                if(m_scene)
                    render_scene = SceneExtractor::extract(*m_scene);
                m_renderer->render_frame(render_scene);
            }
            const auto frame_end = now();
            if(measure) {
                const auto ms = [](auto first, auto last) {
                    return std::chrono::duration<double, std::milli>(last - first)
                        .count();
                };
                m_frame_timing =
                    FrameTiming{update_context.frame_index, ms(frame_start, events_end),
                        ms(events_end, update_end) + ms(prepare_end, simulation_end),
                        ms(update_end, prepare_end), ms(simulation_end, frame_end),
                        ms(frame_start, frame_end), prepared};
            } else {
                m_frame_timing.reset();
            }
        }
    }
}
