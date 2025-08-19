#pragma once
#include "core/engine.h"
#include "core/logger/logger.h"

namespace Comet {
    class Application {
    public:
        virtual ~Application() = default;

        void start() {
            Logger::init();
            m_engine = std::make_unique<Engine>();
            on_init();

            m_engine->register_update_callback([this](const UpdateContext dt) {
                this->on_update(dt);
            });
        }

        void main_loop() const {
            m_engine->on_update();
        }

        void end() {
            on_shutdown();
            Logger::shutdown();
        }

        [[nodiscard]] Engine* get_engine() const { return m_engine.get(); }

        virtual void on_init() = 0;

        virtual void on_update(UpdateContext context) = 0;

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

