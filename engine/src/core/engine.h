#pragma once
#include "common/export.h"
#include "window.h"
#include "render/renderer.h"
#include "timer.h"
#include "common/config.h"

#include <functional>
#include <memory>
#include <vector>

namespace Comet {
    class AssetRegistry;
    class Scene;

    class COMET_API Engine {
    public:
        explicit Engine(Config::Runtime config);

        ~Engine();

        void on_update() const;

        void register_update_callback(const std::function<void(UpdateContext)>& cb) {
            m_update_callbacks.push_back(cb);
        }

        void set_scene(std::unique_ptr<Scene> scene);

        [[nodiscard]] Scene* get_scene() { return m_scene.get(); }
        [[nodiscard]] const Scene* get_scene() const { return m_scene.get(); }

        [[nodiscard]] AssetRegistry* get_asset_registry() { return m_asset_registry.get(); }
        [[nodiscard]] const AssetRegistry* get_asset_registry() const { return m_asset_registry.get(); }

        [[nodiscard]] Window* get_window() const { return m_window.get(); };
        [[nodiscard]] Renderer* get_renderer() const { return m_renderer.get(); };
        [[nodiscard]] const Config::Runtime& get_config() const { return m_config; }
        [[nodiscard]] bool get_close_status() const { return m_should_close; };
        [[nodiscard]] float get_delta_time() const { return m_timer->get_delta_time(); }
        [[nodiscard]] float get_total_time() const { return m_timer->get_total_time(); }
        [[nodiscard]] float get_fps() const { return m_timer->get_fps(); }

    private:
        Config::Runtime m_config;
        bool m_should_close = false;
        std::unique_ptr<Timer> m_timer;
        std::unique_ptr<Window> m_window;
        std::unique_ptr<AssetRegistry> m_asset_registry;
        std::unique_ptr<Scene> m_scene;
        std::unique_ptr<Renderer> m_renderer;
        std::vector<std::function<void(UpdateContext)>> m_update_callbacks;
    };
}
