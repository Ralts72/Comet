#pragma once
#include "../graphics/adapter.h"
#include "../common/singleton.h"

namespace Comet {
    class Engine: public Singleton<Engine, true> {
    public:
        Engine() = default;

        ~Engine();

        void onInit();

        void onUpdate();

        void onEvent(const SDL_Event& event);

        [[nodiscard]] bool getCloseStatus() const { return m_should_close; };

    private:
        bool m_should_close = false;
        std::unique_ptr<Window> m_window;
        std::unique_ptr<Adapter> m_graphics_context;
    };
}
