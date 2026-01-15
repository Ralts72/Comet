#pragma once
#include "core/engine.h"
#include "common/logger.h"
#include "common/config.h"

namespace Comet {
    class Application {
    public:
        virtual ~Application() = default;

        void start() {
            // 1. 初始化 Config
            Config::instance();

            // 2. 初始化 Logger
            Logger::init();
            LOG_INFO("Config and Logger initialized successfully");

            // 3. 创建引擎
            m_engine = std::make_unique<Engine>();

            // 4. 用户初始化代码
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

