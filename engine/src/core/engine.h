#pragma once
#include "common/export.h"
#include "window.h"
#include "render/renderer.h"
#include "timer.h"
#include "config/config.h"

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Comet {
    class AssetRegistry;
    class Scene;
    class TaskScheduler;

    class COMET_API Engine {
    public:
        explicit Engine(const Config& config);

        ~Engine();

        void on_update() const;

        void register_update_callback(std::function<void(UpdateContext)> callback) {
            m_update_callbacks.push_back(std::move(callback));
        }

        void set_scene(std::unique_ptr<Scene> scene);

        [[nodiscard]] std::unique_ptr<Scene> replace_scene(
            std::unique_ptr<Scene> scene) noexcept;

        [[nodiscard]] Scene* get_scene() { return m_scene.get(); }
        [[nodiscard]] const Scene* get_scene() const { return m_scene.get(); }

        [[nodiscard]] AssetRegistry& get_asset_registry() { return *m_asset_registry; }
        [[nodiscard]] const AssetRegistry& get_asset_registry() const {
            return *m_asset_registry;
        }

        [[nodiscard]] ResourceManager& get_resource_manager() {
            return m_renderer->get_resource_manager();
        }
        [[nodiscard]] const ResourceManager& get_resource_manager() const {
            return get_renderer().get_resource_manager();
        }

        [[nodiscard]] TaskScheduler& get_task_scheduler() { return *m_task_scheduler; }
        [[nodiscard]] const TaskScheduler& get_task_scheduler() const {
            return *m_task_scheduler;
        }

        [[nodiscard]] Window& get_window() { return *m_window; }
        [[nodiscard]] const Window& get_window() const { return *m_window; }
        [[nodiscard]] Renderer& get_renderer() { return *m_renderer; }
        [[nodiscard]] const Renderer& get_renderer() const { return *m_renderer; }

    private:
        std::unique_ptr<Timer> m_timer;
        std::unique_ptr<TaskScheduler> m_task_scheduler;
        std::unique_ptr<Window> m_window;
        std::unique_ptr<AssetRegistry> m_asset_registry;
        std::unique_ptr<Scene> m_scene;
        std::unique_ptr<Renderer> m_renderer;
        std::vector<std::function<void(UpdateContext)>> m_update_callbacks;
    };
}
