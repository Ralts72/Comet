#include "runtime.h"
#include "../core/engine.h"

namespace Comet {
    void run(Application* app) {
        LOG_INFO("[Runtime] Init logger");
        LOG_INFO("[Runtime] App starting...");
        Engine::init();
        // SDL_AppInit();
        Engine::getInstance().onInit();
        app->onInit();
        Engine::getInstance().onUpdate();
        app->onUpdate();
        app->onShutdown();

        Engine::shutdown();
        LOG_INFO("[Runtime] Clean shutdown");
    }
}
