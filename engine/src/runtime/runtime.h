#pragma once
#include "core/engine.h"

namespace Comet {
    class Application {
    public:
        virtual ~Application() = default;

        void start() {
            Engine::init();
            on_init();
        }

        void main_loop() {
            Engine::instance().on_update();
            on_update();
        }

        void end() {
            on_shutdown();
            Engine::shutdown();
        }

        virtual void on_init() = 0;

        virtual void on_update(float delta_time = 0.0f) = 0;

        virtual void on_shutdown() = 0;
    };

    inline void run(Application* app) {
        app->start();
        app->main_loop();
        app->end();
    }
}

