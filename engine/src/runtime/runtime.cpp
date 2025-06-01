#include "runtime.h"
#include "../core/engine.h"

namespace Comet {
    void run(Application* app) {
        LOG_INFO("[Runtime] Init logger");
        LOG_INFO("[Runtime] App starting...");
        Engine::init();

        app->onInit();
        SDL_Event event;
        Engine::getInstance().onUpdate();
        Engine::getInstance().onEvent(event);
        app->onUpdate();

        app->onShutdown();
        Engine::shutdown();
        LOG_INFO("[Runtime] Clean shutdown");
    }
}
