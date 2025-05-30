#include "engine.h"

namespace Comet {
    Engine::~Engine() {
        LOG_INFO("shutdown graphics system");
        m_graphics_context.reset();

        LOG_INFO("shutdown window system");
        m_window.reset();
    }

    void Engine::onInit() {
        LOG_INFO("init SDL");
        if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_SENSOR |
                     SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
            LOG_FATAL("SDL init failed: {}", SDL_GetError());
        }

        LOG_INFO("init window");
        m_window = std::make_unique<Window>("Comet Engine", 1024, 720);

        LOG_INFO("init graphics system");
        m_graphics_context = std::make_unique<Adapter>(*m_window);

        LOG_INFO("running engine...");
    }

    void Engine::onUpdate() {}

    void Engine::onEvent(const SDL_Event& event) {
        if(event.type == SDL_EVENT_QUIT) {
            m_should_close = true;
        }
    }

    SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
        Comet::Engine::init();
        if(Comet::Engine::getInstance().getCloseStatus()) {
            return SDL_APP_FAILURE;
        }
        return SDL_APP_CONTINUE;
    }

    SDL_AppResult SDL_AppIterate(void* appstate) {
        auto& engine = Comet::Engine::getInstance();
        engine.onUpdate();
        if(engine.getCloseStatus()) {
            return SDL_APP_SUCCESS;
        }
        return SDL_APP_CONTINUE;
    }

    SDL_AppResult SDL_AppEvent(void* appstate, const SDL_Event* event) {
        Comet::Engine::getInstance().onEvent(*event);
        return SDL_APP_CONTINUE;
    }

    void SDL_AppQuit(void* appstate, SDL_AppResult result) {
        Comet::Engine::shutdown();
    }
}
