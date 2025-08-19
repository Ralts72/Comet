#pragma once
#include "core/engine.h"
#include "core/logger/logger.h"

namespace Comet {
    class Application {
    public:
        virtual ~Application() = default;

        void start() {
            Logger::init();
            on_init();
            m_engine = std::make_unique<Engine>(m_settings);

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

    protected:
        void set_width(const int width) {
            m_settings.width = width;
        }
        void set_height(const int height) {
            m_settings.height = height;
        }
        void set_title(const std::string& title) {
            m_settings.title = title;
        }
        void set_settings(const Settings& settings) {
            m_settings = settings;
        }

    private:
        std::unique_ptr<Engine> m_engine;
        Settings m_settings = {
            .width = 1280,
            .height = 720,
            .title = "Comet Engine"
        };
    };

    inline void run(Application* app) {
        app->start();
        app->main_loop();
        app->end();
    }
}

