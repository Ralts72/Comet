#include "engine.h"
#include "common/scope_exit.h"
#include "config/config.h"
#include "core/window.h"
#include "graphics/device.h"
#include "render/renderer.h"
#include "render/render_diagnostics.h"
#include "render/render_context.h"
#include "render/resource/render_resources.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "render/scene/scene_extractor.h"
#include "scene/scene.h"

namespace Comet {
    Result<std::unique_ptr<Engine>, Error> Engine::create(const Config& config) {
        PROFILE_SCOPE("Engine::Constructor");
        LOG_INFO("init window");
        auto window = std::make_unique<Window>(config.window);
        auto assets = std::make_unique<AssetRegistry>();
        LOG_INFO("init renderer");
        auto renderer = Renderer::create(*window, config, *assets);
        if(!renderer)
            return Result<std::unique_ptr<Engine>, Error>::failure(renderer.error().as_error());
        auto scheduler = std::make_unique<TaskScheduler>();
        return Result<std::unique_ptr<Engine>, Error>::success(
            std::unique_ptr<Engine>(new Engine(std::move(window), std::move(assets),
                std::move(renderer).value(), std::move(scheduler))));
    }

    Engine::Engine(std::unique_ptr<Window> window, std::unique_ptr<AssetRegistry> assets,
        std::unique_ptr<Renderer> renderer, std::unique_ptr<TaskScheduler> scheduler)
        : m_timer(std::make_unique<Timer>()), m_task_scheduler(std::move(scheduler)),
          m_window(std::move(window)), m_asset_registry(std::move(assets)),
          m_renderer(std::move(renderer)) {}

    Engine::~Engine() {
        LOG_INFO("shutting down engine...");
        prepare_shutdown();
        m_asset_registry->clear();
        m_renderer.reset();
        m_asset_registry.reset();
        m_scene.reset();
        m_window.reset();
        m_task_scheduler.reset();
    }

    void Engine::prepare_shutdown() {
        if(m_shutdown_prepared)
            return;
        if(auto stopped = stop_scene_runtime(); !stopped)
            LOG_FATAL("Cannot shut down Engine during System execution");
        if(auto cleared = m_scene_runtime.clear_systems(); !cleared)
            LOG_FATAL("Cannot release stopped scene systems");
        m_task_scheduler->shutdown();
        m_renderer->prepare_shutdown();
        m_shutdown_prepared = true;
    }

    RenderResources& Engine::get_render_resources() {
        return m_renderer->get_render_resources();
    }

    const RenderResources& Engine::get_render_resources() const {
        return get_renderer().get_render_resources();
    }

    void Engine::set_scene(std::unique_ptr<Scene> scene) {
        static_cast<void>(replace_scene(std::move(scene)));
    }

    const Input::Frame& Engine::get_input_frame() const {
        return m_window->get_input_frame();
    }

    Result<void, Error> Engine::add_system(std::unique_ptr<System> system) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        return m_scene_runtime.add_system(std::move(system));
    }

    Result<void, Error> Engine::set_runtime_settings(SceneRuntime::Settings settings) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        return m_scene_runtime.set_settings(settings);
    }

    Result<void, Error> Engine::start_scene_runtime() {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        if(!m_scene)
            return Result<void, Error>::failure({"Cannot start runtime without an active scene"});
        return m_scene_runtime.start(*m_scene);
    }

    Result<void, Error> Engine::stop_scene_runtime() {
        auto stopped = m_scene_runtime.stop();
        if(stopped)
            m_runtime_input.reset();
        return stopped;
    }

    Result<void, Error> Engine::set_runtime_state(SceneRuntime::State state) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        return m_scene_runtime.set_state(state);
    }

    Result<void, Error> Engine::request_runtime_step() {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        return m_scene_runtime.request_step();
    }

    std::unique_ptr<Scene> Engine::replace_scene(std::unique_ptr<Scene> scene) noexcept {
        // System 执行时不得销毁其借用的 Scene；换场景请求须交由宿主下一次更新处理。
        if(auto stopped = stop_scene_runtime(); !stopped)
            LOG_FATAL("Cannot replace Scene during System execution");
        m_scene.swap(scene);
        return scene;
    }

    Result<void, Error> Engine::run(const std::function<Result<void, Error>(UpdateContext)>& update,
        const std::function<Result<void, Error>()>& frame_ready) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        if(m_running)
            return Result<void, Error>::failure({"Engine update loop is already running"});
        m_running = true;
        const ScopeExit reset_running([&] { m_running = false; });
        LOG_INFO("running engine...");

        while(!m_window->should_close()) {
            if(auto frame = tick(update, frame_ready); !frame)
                return frame;
        }
        return Result<void, Error>::success();
    }

    Result<void, Error> Engine::tick(
        const std::function<Result<void, Error>(UpdateContext)>& update,
        const std::function<Result<void, Error>()>& frame_ready) {
        PROFILE_SCOPE("Engine::Frame");
        const bool capture = m_renderer->get_diagnostics().is_enabled();
        using Clock = std::chrono::steady_clock;
        auto phase_start = capture ? Clock::now() : Clock::time_point{};
        const auto phase_ms = [&] {
            if(!capture)
                return 0.0;
            const auto now = Clock::now();
            const auto duration =
                std::chrono::duration<double, std::milli>(now - phase_start).count();
            phase_start = now;
            return duration;
        };
        FrameTiming timing;
        const auto publish = [&] {
            if(capture && m_renderer->get_diagnostics().is_enabled()) {
                timing.total_ms = timing.events_ms + timing.update_ms + timing.prepare_ms
                                  + timing.render_submit_ms;
                if(!m_frame_timing)
                    m_frame_history.clear();
                const TimingHistory::Entry phases[]{{"Events", timing.events_ms},
                    {"Update", timing.update_ms}, {"Prepare / UI", timing.prepare_ms},
                    {"Render / submit", timing.render_submit_ms}};
                m_frame_history.record(timing.total_ms, phases);
                m_frame_timing = timing;
            } else {
                m_frame_timing.reset();
            }
        };
        if(!capture)
            m_frame_timing.reset();
        m_window->poll_events();
        if(m_window->should_close())
            return Result<void, Error>::success();

        const auto framebuffer_size = m_window->get_framebuffer_size();
        if(framebuffer_size.x == 0 || framebuffer_size.y == 0) {
            m_frame_timing.reset();
            m_window->wait_events();
            m_timer->tick();
            return Result<void, Error>::success();
        }

        m_runtime_input.reset();
        m_window->publish_input_frame();
        timing.events_ms = phase_ms();
        m_timer->tick();
        timing.frame_index = m_timer->get_update_context().frame_index;
        if(update) {
            if(auto result = update(m_timer->get_update_context()); !result)
                return result;
        }
        if(m_window->should_close())
            return Result<void, Error>::success();

        timing.update_ms = phase_ms();
        const auto preparation = m_renderer->prepare_frame();
        if(!preparation)
            return Result<void, Error>::failure(preparation.error().as_error());
        if(preparation.value() && frame_ready) {
            if(auto edited = frame_ready(); !edited) {
                // 已获取的帧不再重用；交互失败终止本次引擎生命周期。
                prepare_shutdown();
                return edited;
            }
        }

        timing.prepare_ms = phase_ms();
        if(auto advanced = m_scene_runtime.advance(m_timer->get_update_context().delta_time,
               m_runtime_input ? &*m_runtime_input : nullptr);
            !advanced) {
            prepare_shutdown();
            return advanced;
        }
        timing.update_ms += phase_ms();
        if(!preparation.value()) {
            m_window->wait_events(0.016);
            timing.prepare_ms += phase_ms();
            publish();
            return Result<void, Error>::success();
        }
        RenderScene render_scene;
        if(m_scene)
            render_scene = SceneExtractor::extract(*m_scene);
        const auto rendered = m_renderer->render_frame(render_scene);
        if(!rendered)
            return Result<void, Error>::failure(rendered.error().as_error());
        timing.render_submit_ms = phase_ms();
        timing.rendered = true;
        publish();
        return Result<void, Error>::success();
    }
}
