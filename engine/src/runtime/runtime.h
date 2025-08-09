#pragma once
#include "core/engine.h"

namespace Comet {
    class Application {
    public:
        virtual ~Application() = default;

        virtual void on_init() = 0;

        virtual void on_update() = 0;

        virtual void on_shutdown() = 0;
    };

    inline void run(Application* app) {
        Engine::init();
        app->on_init();
        Engine::instance().on_update();
        app->on_update();
        Engine::shutdown();
        app->on_shutdown();
    }
}

