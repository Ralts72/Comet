#pragma once
#include "core/engine.h"
#include "common/log_system/log_system.h"
#include <memory>

namespace Comet {
    class Application {
    public:
        virtual ~Application() = default;

        void start() {
            LogSystem::init();
            m_engine = std::make_unique<Engine>();
            on_init();
        }

        void main_loop() {
            m_engine->on_update();
            on_update(0.0f);
        }

        void end() {
            on_shutdown();
            m_engine.reset();
            LogSystem::shutdown();
        }

        [[nodiscard]] Engine* get_engine() const { return m_engine.get(); }

        virtual void on_init() = 0;

        virtual void on_update(float delta_time) = 0;

        virtual void on_shutdown() = 0;
        
    private:
        std::unique_ptr<Engine> m_engine;
    };

    inline void run(Application* app) {
        app->start();
        app->main_loop();
        app->end();
    }
}

