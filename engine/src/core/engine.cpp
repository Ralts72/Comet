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
#include "scene/systems/audio_system.h"
#include "scene/systems/camera_controller.h"
#include "scene/systems/physics_system.h"
#include "scene/systems/script_system.h"

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

    Result<void, Error> Engine::add_default_scene_systems() {
        // 注册顺序也是各更新阶段的执行顺序；SceneRuntime 停止时按逆序清理。
        if(auto added = add_system(std::make_unique<CameraControllerSystem>()); !added)
            return added;
        if(auto added = add_system(std::make_unique<ScriptSystem>(*m_asset_registry)); !added)
            return added;
        if(auto added = add_system(std::make_unique<PhysicsSystem>()); !added)
            return added;
        return add_system(std::make_unique<AudioSystem>(*m_asset_registry));
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

    Result<void, Error> Engine::set_input_actions(InputActions actions) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        return m_scene_runtime.set_input_actions(std::move(actions));
    }

    Result<void, Error> Engine::stop_scene_runtime() {
        return m_scene_runtime.stop();
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

    Result<void, Error> Engine::run(const std::function<Result<void, Error>(FrameContext&)>& update,
        const std::function<Result<void, Error>(FrameContext&)>& frame_ready,
        const std::function<Result<void, Error>(const Error&)>& runtime_failed) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        if(m_running)
            return Result<void, Error>::failure({"Engine update loop is already running"});
        m_running = true;
        const ScopeExit reset_running([&] { m_running = false; });
        LOG_INFO("running engine...");

        while(!m_window->should_close()) {
            if(auto frame = tick(update, frame_ready, runtime_failed); !frame)
                return frame;
        }
        return Result<void, Error>::success();
    }

    Result<void, Error> Engine::tick(
        const std::function<Result<void, Error>(FrameContext&)>& update,
        const std::function<Result<void, Error>(FrameContext&)>& frame_ready,
        const std::function<Result<void, Error>(const Error&)>& runtime_failed) {
        PROFILE_SCOPE("Engine::Frame");
        m_frame_diagnostics.begin_frame(m_renderer->get_diagnostics().is_enabled());
        m_window->poll_events();
        if(m_window->should_close())
            return Result<void, Error>::success();

        const auto framebuffer_size = m_window->get_framebuffer_size();
        if(framebuffer_size.x == 0 || framebuffer_size.y == 0) {
            m_frame_diagnostics.clear_current();
            if(auto discarded = m_scene_runtime.discard_input(); !discarded)
                return discarded;
            m_window->wait_events();
            m_window->discard_pending_input();
            m_timer->tick();
            return Result<void, Error>::success();
        }

        m_window->publish_input_frame();
        m_frame_diagnostics.mark_events();
        m_timer->tick();
        FrameContext frame{m_timer->get_update_context(), m_window->get_input_frame(), {}};
        m_frame_diagnostics.set_frame_index(frame.update.frame_index);
        if(update) {
            if(auto result = update(frame); !result)
                return result;
        }
        if(m_window->should_close())
            return Result<void, Error>::success();

        m_frame_diagnostics.mark_update();
        const auto preparation = m_renderer->prepare_frame();
        if(!preparation) {
            prepare_shutdown();
            return Result<void, Error>::failure(preparation.error().as_error());
        }
        const bool frame_ready_to_render = preparation.value() == Renderer::FramePreparation::Ready;
        if(frame_ready_to_render && frame_ready) {
            if(auto edited = frame_ready(frame); !edited) {
                // 已获取的帧不再重用；交互失败终止本次引擎生命周期。
                prepare_shutdown();
                return edited;
            }
        }

        m_frame_diagnostics.mark_prepare();
        if(auto advanced = m_scene_runtime.advance(
               frame.update.delta_time, frame.runtime_input ? &*frame.runtime_input : nullptr);
            !advanced) {
            if(!runtime_failed || is_device_lost(advanced.error())) {
                prepare_shutdown();
                return advanced;
            }
            // 不提取部分写入的场景；已 acquire 的帧先用空场景完成，再交给宿主恢复。
            if(frame_ready_to_render) {
                if(auto drained = m_renderer->render_frame({}); !drained) {
                    prepare_shutdown();
                    return Result<void, Error>::failure(drained.error().as_error());
                }
            }
            if(auto recovered = runtime_failed(advanced.error()); !recovered) {
                prepare_shutdown();
                return recovered;
            }
            return Result<void, Error>::success();
        }
        m_frame_diagnostics.mark_runtime_update();
        if(!frame_ready_to_render) {
            m_window->wait_events(0.016);
            m_frame_diagnostics.mark_deferred_wait();
            m_frame_diagnostics.finish_frame(false, m_renderer->get_diagnostics().is_enabled());
            return Result<void, Error>::success();
        }
        RenderScene render_scene;
        if(m_scene)
            render_scene = SceneExtractor::extract(*m_scene);
        const auto rendered = m_renderer->render_frame(render_scene);
        if(!rendered) {
            prepare_shutdown();
            return Result<void, Error>::failure(rendered.error().as_error());
        }
        m_frame_diagnostics.mark_render_submit();
        m_frame_diagnostics.finish_frame(true, m_renderer->get_diagnostics().is_enabled());
        return Result<void, Error>::success();
    }
}
