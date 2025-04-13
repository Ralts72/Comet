#pragma once
#include "../engine/src/core/Window.h"

namespace App {

    class Application
    {
    public:
        ~Application();
        Application(const Application &) = delete;
        Application &operator=(const Application &) = delete;

        static Application &ref()
        {
            static Application instance;
            return instance;
        }

        void update();
        // void onEvent(Engine::Event &e);

        void init();
        bool isRunning() const { return m_running; }

    private:
        Application() = default;

        // bool onWindowClose(Engine::Event &e);
        // bool onWindowResize(Engine::Event &e);

        bool m_running = true;
        std::unique_ptr<Comet::Window> m_window;
    };

    static Application &app = Application::ref();

}