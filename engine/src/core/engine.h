#pragma once
#include "common/export.h"
#include "window.h"
#include "render/renderer.h"
#include "timer.h"
#include "common/config.h"

namespace Comet {
    class COMET_API Engine {
    public:
        explicit Engine(Config::Runtime config);

        ~Engine();

        void on_update() const;

        void register_update_callback(const std::function<void(UpdateContext)>& cb) {
            m_update_callbacks.push_back(cb);
        }

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
        std::unique_ptr<Renderer> m_renderer;
        std::vector<std::function<void(UpdateContext)>> m_update_callbacks;
    };
}
