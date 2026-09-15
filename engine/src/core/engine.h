#pragma once
#include "common/export.h"
#include "common/error.h"
#include "common/result.h"
#include "timer.h"

#include <functional>
#include <memory>
#include <utility>

namespace Comet {
    class AssetRegistry;
    class Scene;
    class TaskScheduler;
    class Window;
    class Renderer;
    class ResourceManager;
    class Config;

    class COMET_API Engine {
    public:
        static Result<std::unique_ptr<Engine>, Error> create(const Config& config);

        ~Engine();

        // 终止生命周期时调用；先完成后台工作和 GPU 使用，再释放应用持有的资源。
        void prepare_shutdown();

        // 同步运行；更新函数仅在本次调用期间使用，不保存到引擎中。
        [[nodiscard]] Result<void, Error> run(
            const std::function<Result<void, Error>(UpdateContext)>& update = {},
            const std::function<Result<void, Error>()>& frame_ready = {});

        void set_scene(std::unique_ptr<Scene> scene);

        [[nodiscard]] std::unique_ptr<Scene> replace_scene(std::unique_ptr<Scene> scene) noexcept;

        [[nodiscard]] Scene* get_scene() { return m_scene.get(); }
        [[nodiscard]] const Scene* get_scene() const { return m_scene.get(); }

        [[nodiscard]] AssetRegistry& get_asset_registry() { return *m_asset_registry; }
        [[nodiscard]] const AssetRegistry& get_asset_registry() const { return *m_asset_registry; }

        [[nodiscard]] ResourceManager& get_resource_manager();
        [[nodiscard]] const ResourceManager& get_resource_manager() const;

        [[nodiscard]] TaskScheduler& get_task_scheduler() { return *m_task_scheduler; }
        [[nodiscard]] const TaskScheduler& get_task_scheduler() const { return *m_task_scheduler; }

        [[nodiscard]] Window& get_window() { return *m_window; }
        [[nodiscard]] const Window& get_window() const { return *m_window; }
        [[nodiscard]] Renderer& get_renderer() { return *m_renderer; }
        [[nodiscard]] const Renderer& get_renderer() const { return *m_renderer; }

    private:
        Engine(std::unique_ptr<Window> window, std::unique_ptr<AssetRegistry> assets,
            std::unique_ptr<Renderer> renderer, std::unique_ptr<TaskScheduler> scheduler);
        [[nodiscard]] Result<void, Error> tick(
            const std::function<Result<void, Error>(UpdateContext)>& update,
            const std::function<Result<void, Error>()>& frame_ready);
        std::unique_ptr<Timer> m_timer;
        std::unique_ptr<TaskScheduler> m_task_scheduler;
        std::unique_ptr<Window> m_window;
        std::unique_ptr<AssetRegistry> m_asset_registry;
        std::unique_ptr<Scene> m_scene;
        std::unique_ptr<Renderer> m_renderer;
        bool m_running = false;
        bool m_shutdown_prepared = false;
    };
}
