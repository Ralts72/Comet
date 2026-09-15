#include "engine.h"
#include "common/scope_exit.h"
#include "config/config.h"
#include "core/window.h"
#include "graphics/device.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "render/resource/resource_manager.h"
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
        m_task_scheduler->shutdown();
        m_renderer->prepare_shutdown();
        m_shutdown_prepared = true;
    }

    ResourceManager& Engine::get_resource_manager() {
        return m_renderer->get_resource_manager();
    }

    const ResourceManager& Engine::get_resource_manager() const {
        return get_renderer().get_resource_manager();
    }

    void Engine::set_scene(std::unique_ptr<Scene> scene) {
        m_scene = std::move(scene);
    }

    std::unique_ptr<Scene> Engine::replace_scene(std::unique_ptr<Scene> scene) noexcept {
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
        m_window->poll_events();
        if(m_window->should_close())
            return Result<void, Error>::success();

        const auto framebuffer_size = m_window->get_framebuffer_size();
        if(framebuffer_size.x == 0 || framebuffer_size.y == 0) {
            m_window->wait_events();
            m_timer->tick();
            return Result<void, Error>::success();
        }

        m_timer->tick();
        if(update) {
            if(auto result = update(m_timer->get_update_context()); !result)
                return result;
        }
        if(m_window->should_close())
            return Result<void, Error>::success();

        const auto preparation = m_renderer->prepare_frame();
        if(!preparation)
            return Result<void, Error>::failure(preparation.error().as_error());
        if(!preparation.value()) {
            m_window->wait_events(0.016);
            return Result<void, Error>::success();
        }

        if(frame_ready) {
            if(auto edited = frame_ready(); !edited) {
                // 已获取的帧不再重用；交互失败终止本次引擎生命周期。
                prepare_shutdown();
                return edited;
            }
        }

        RenderScene render_scene;
        if(m_scene)
            render_scene = SceneExtractor::extract(*m_scene);
        const auto rendered = m_renderer->render_frame(render_scene);
        if(!rendered)
            return Result<void, Error>::failure(rendered.error().as_error());
        return Result<void, Error>::success();
    }
}
