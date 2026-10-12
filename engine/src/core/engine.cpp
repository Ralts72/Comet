#include "engine.h"
#include "common/scope_exit.h"
#include "config/config.h"
#include "core/window.h"
#include "graphics/device.h"
#include "render/renderer.h"
#include "render/render_thread.h"
#include "render/material/material_programs.h"
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

#include <thread>

namespace Comet {
    Result<std::unique_ptr<Engine>, Error> Engine::create(const Config& config) {
        PROFILE_SCOPE("Engine::Constructor");
        LOG_INFO("init window");
        auto window = std::make_unique<Window>(config.window);
        auto assets = std::make_unique<AssetRegistry>();
        LOG_INFO("init renderer");
        const Renderer::Settings renderer_settings{
            config.vulkan, config.render, config.diagnostics.enable_render_diagnostics};
        auto renderer = Renderer::create(*window, renderer_settings);
        if(!renderer)
            return Result<std::unique_ptr<Engine>, Error>::failure(renderer.error().as_error());
        auto scheduler = std::make_unique<TaskScheduler>();
        return Result<std::unique_ptr<Engine>, Error>::success(
            std::unique_ptr<Engine>(new Engine(std::move(window), std::move(assets),
                std::move(renderer).value(), std::move(scheduler))));
    }

    Engine::Engine(std::unique_ptr<Window> window, std::unique_ptr<AssetRegistry> assets,
        std::unique_ptr<Renderer> renderer, std::unique_ptr<TaskScheduler> scheduler)
        : m_frame_timer(std::make_unique<FrameTimer>()), m_task_scheduler(std::move(scheduler)),
          m_window(std::move(window)), m_asset_registry(std::move(assets)),
          m_renderer(std::move(renderer)),
          m_render_thread(std::make_unique<RenderThread>(*m_renderer)),
          m_material_parameters(std::make_unique<MaterialProgramValidator>(*m_asset_registry)),
          m_scene_resolver(*m_asset_registry) {
        m_renderer->m_render_thread = m_render_thread.get();
    }

    Engine::~Engine() {
        LOG_INFO("shutting down engine...");
        prepare_shutdown();
        m_asset_registry->clear();
        m_material_parameters.reset();
        m_renderer->m_render_thread = nullptr;
        m_render_thread.reset();
        m_renderer.reset();
        m_asset_registry.reset();
        m_scene.reset();
        m_window.reset();
        m_task_scheduler.reset();
    }

    void Engine::prepare_shutdown() {
        if(m_shutdown_prepared)
            return;
        static_cast<void>(complete_render_frame());
        m_render_thread->stop();
        m_renderer->end_render_loop();
        if(auto stopped = stop_scene_runtime(); !stopped)
            LOG_FATAL("Cannot shut down Engine during System execution");
        if(auto cleared = m_scene_runtime.clear_systems(); !cleared)
            LOG_FATAL("Cannot release stopped scene systems");
        if(auto detached = m_scene_runtime.set_services({}); !detached)
            LOG_FATAL("Cannot detach stopped runtime services");
        m_audio_service.reset();
        m_physics_service.reset();
        m_task_scheduler->shutdown();
        m_scene_extractor.reset();
        m_scene_resolver.reset();
        m_renderer->prepare_shutdown();
        m_shutdown_prepared = true;
    }

    RenderResources& Engine::get_render_resources() {
        return m_renderer->get_render_resources();
    }

    const RenderResources& Engine::get_render_resources() const {
        return get_renderer().get_render_resources();
    }

    RenderResourceFactory& Engine::get_render_resource_factory() {
        return *m_render_thread;
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

    Result<void, Error> Engine::apply_audio_settings(const AudioSettings settings) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        if(auto valid = settings.validate(); !valid)
            return Result<void, Error>::failure({valid.error()});
        if(m_audio_service) {
            if(auto applied = m_audio_service->apply_settings(settings); !applied)
                return applied;
        }
        m_audio_settings = settings;
        return Result<void, Error>::success();
    }

    Result<void, Error> Engine::add_default_scene_systems() {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        if(!m_audio_service) {
            m_audio_service = std::make_unique<AudioService>(*m_asset_registry);
            if(auto applied = m_audio_service->apply_settings(m_audio_settings); !applied)
                return applied;
        }
        if(!m_physics_service)
            m_physics_service = std::make_unique<PhysicsService>();
        if(auto configured = m_scene_runtime.set_services(
               {.audio = m_audio_service.get(), .physics = m_physics_service.get()});
            !configured)
            return configured;
        // 注册顺序也是各更新阶段的执行顺序；SceneRuntime 停止时按逆序清理。
        if(auto added = add_system(std::make_unique<CameraControllerSystem>()); !added)
            return added;
        if(auto added = add_system(
               std::make_unique<ScriptSystem>(*m_asset_registry, m_material_parameters.get()));
            !added)
            return added;
        if(auto added = add_system(std::make_unique<PhysicsSystem>(*m_physics_service)); !added)
            return added;
        return add_system(std::make_unique<AudioSystem>(*m_audio_service));
    }

    Result<void, Error> Engine::set_runtime_settings(SceneRuntime::Settings settings) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        return m_scene_runtime.set_settings(settings);
    }

    Result<void, Error> Engine::start_scene_runtime(const SceneRuntime::State state) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        if(!m_scene)
            return Result<void, Error>::failure({"Cannot start runtime without an active scene"});
        m_material_parameters->accept(m_renderer->get_material_program_feedback());
        return m_scene_runtime.start(*m_scene, state, SceneRuntime::InputStart::Rebase);
    }

    Result<void, Error> Engine::set_input_actions(InputActions actions) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        return m_scene_runtime.set_input_actions(std::move(actions));
    }

    Result<void, Error> Engine::stop_scene_runtime() {
        auto stopped = m_scene_runtime.stop();
        if(stopped)
            m_window->set_cursor_locked(false);
        return stopped;
    }

    Result<void, Error> Engine::rebind_input_actions(InputActions actions) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        return m_scene_runtime.rebind_input_actions(std::move(actions));
    }

    Result<void, Error> Engine::set_runtime_state(SceneRuntime::State state) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        auto changed = m_scene_runtime.set_state(state);
        if(changed && state == SceneRuntime::State::Paused)
            m_window->set_cursor_locked(false);
        return changed;
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
        m_scene_extractor.reset();
        return scene;
    }

    Result<void, Error> Engine::run(const Callbacks& callbacks) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        if(m_running)
            return Result<void, Error>::failure({"Engine update loop is already running"});
        m_running = true;
        const ScopeExit reset_running([&] {
            static_cast<void>(complete_render_frame());
            m_renderer->end_render_loop();
            m_window->set_cursor_locked(false);
            m_running = false;
        });
        if(auto started = m_renderer->begin_render_loop(); !started)
            return Result<void, Error>::failure(started.error().as_error());
        LOG_INFO("running engine...");

        auto result = Result<void, Error>::success();
        while(!m_window->should_close()) {
            const auto frame_start = FramePacer::Clock::now();
            result = tick(callbacks);
            if(!result)
                break;
            if(!m_window->should_close()) {
                if(auto deadline = m_frame_pacer.deadline(frame_start))
                    std::this_thread::sleep_until(*deadline);
            }
        }
        if(auto completed = complete_render_frame(); !completed) {
            prepare_shutdown();
            return completed;
        }
        return result;
    }

    Result<void, Error> Engine::complete_render_frame() {
        if(m_render_thread) {
            if(auto completed = m_render_thread->complete(); !completed)
                return Result<void, Error>::failure(completed.error().as_error());
        }
        return Result<void, Error>::success();
    }

    Result<void, Error> Engine::set_frame_rate_limit(int limit) {
        if(m_shutdown_prepared)
            return Result<void, Error>::failure({"Engine is shutting down"});
        if(auto valid = m_frame_pacer.set_limit(limit); !valid)
            return Result<void, Error>::failure({valid.error()});
        return Result<void, Error>::success();
    }

    Result<void, Error> Engine::tick(const Callbacks& callbacks) {
        PROFILE_SCOPE("Engine::Frame");
        m_frame_diagnostics.begin_frame(m_renderer->get_diagnostics().is_enabled());
        ScopeExit discard_unfinished_diagnostics([this] { m_frame_diagnostics.clear_current(); });
        m_window->poll_events();
        m_frame_diagnostics.mark_events();
        if(auto completed = complete_render_frame(); !completed) {
            prepare_shutdown();
            return completed;
        }
        m_frame_diagnostics.mark_render_wait();
        if(m_window->should_close()) {
            m_frame_diagnostics.cancel_pending_frame();
            discard_unfinished_diagnostics.release();
            return Result<void, Error>::success();
        }

        const auto framebuffer_size = m_window->get_framebuffer_size();
        if(framebuffer_size.x == 0 || framebuffer_size.y == 0) {
            m_window->set_cursor_locked(false);
            if(auto discarded = m_scene_runtime.discard_input(); !discarded)
                return discarded;
            m_window->wait_events();
            m_window->discard_pending_input();
            m_frame_timer->tick();
            return Result<void, Error>::success();
        }

        m_window->publish_input_frame();
        m_frame_timer->tick();
        const FrameContext frame{m_frame_timer->get_update_context(), m_window->get_input_frame()};
        m_frame_diagnostics.set_frame_index(frame.update.frame_index);
        m_material_parameters->accept(m_renderer->get_material_program_feedback());
        if(callbacks.update) {
            if(auto result = callbacks.update(frame); !result)
                return result;
        }
        if(m_window->should_close()) {
            m_frame_diagnostics.cancel_pending_frame();
            discard_unfinished_diagnostics.release();
            return Result<void, Error>::success();
        }

        m_frame_diagnostics.mark_update();
        m_scene_resolver.refresh_assets();
        const auto preparation = m_renderer->prepare_frame(m_asset_registry->take_removals());
        if(!preparation) {
            prepare_shutdown();
            return Result<void, Error>::failure(preparation.error().as_error());
        }
        const bool frame_ready_to_render = preparation.value() == Renderer::FramePreparation::Ready;
        m_material_parameters->accept(m_renderer->get_material_program_feedback());
        if(frame_ready_to_render && callbacks.frame_ready) {
            if(auto edited = callbacks.frame_ready(frame); !edited) {
                // 已获取的帧不再重用；交互失败终止本次引擎生命周期。
                prepare_shutdown();
                return edited;
            }
        }

        m_frame_diagnostics.mark_prepare();
        std::optional<Input::Frame> runtime_input;
        if(callbacks.runtime_input)
            runtime_input = callbacks.runtime_input(frame);
        if(auto advanced = m_scene_runtime.advance(
               frame.update.delta_time, runtime_input ? &*runtime_input : nullptr);
            !advanced) {
            m_window->set_cursor_locked(false);
            if(!callbacks.runtime_failed || is_device_lost(advanced.error())) {
                prepare_shutdown();
                return advanced;
            }
            m_scene_resolver.reset();
            // 不提取部分写入的场景；先完成已 acquire 的帧，再交给宿主恢复。
            if(frame_ready_to_render) {
                if(auto queued = m_render_thread->submit(m_renderer->capture_frame()); !queued) {
                    prepare_shutdown();
                    return Result<void, Error>::failure(queued.error().as_error());
                }
                if(auto drained = complete_render_frame(); !drained) {
                    prepare_shutdown();
                    return drained;
                }
            }
            if(auto recovered = callbacks.runtime_failed(advanced.error()); !recovered) {
                prepare_shutdown();
                return recovered;
            }
            return Result<void, Error>::success();
        }
        m_window->set_cursor_locked(m_scene_runtime.wants_cursor_capture());
        m_frame_diagnostics.mark_runtime_update();
        if(!frame_ready_to_render) {
            m_window->wait_events(0.016);
            m_frame_diagnostics.mark_deferred_wait();
            m_frame_diagnostics.finish_frame(false, m_renderer->get_diagnostics().is_enabled());
            discard_unfinished_diagnostics.release();
            return Result<void, Error>::success();
        }
        auto render_frame = m_renderer->capture_frame();
        if(m_scene) {
            const auto render_scene = m_scene_extractor.update(*m_scene);
            m_frame_diagnostics.mark_scene_extract();
            if(render_frame.has_visible_view()) {
                render_frame.world = RenderDiagnostics::measure_preparation(
                    &m_renderer->get_diagnostics(), RenderDiagnostics::PreparationPhase::Assets,
                    [&] { return m_scene_resolver.update(*render_scene); });
            } else {
                m_scene_resolver.reset();
            }
        } else {
            m_scene_extractor.reset();
            m_scene_resolver.reset();
        }
        const auto rendered = m_render_thread->submit(std::move(render_frame));
        if(!rendered) {
            prepare_shutdown();
            return Result<void, Error>::failure(rendered.error().as_error());
        }
        m_frame_diagnostics.mark_frame_handoff();
        m_frame_diagnostics.finish_frame(true, m_renderer->get_diagnostics().is_enabled());
        discard_unfinished_diagnostics.release();
        return Result<void, Error>::success();
    }
}
