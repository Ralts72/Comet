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

            // 3. 从 Config 设置窗口参数
            m_settings.width = Config::get<int>("window.width", 1280);
            m_settings.height = Config::get<int>("window.height", 720);
            m_settings.title = Config::get<std::string>("window.title", "Comet Engine");
            LOG_DEBUG("Window settings loaded from config: {}x{}, title: '{}'",
                     m_settings.width, m_settings.height, m_settings.title);

            // 4. 用户初始化代码（可以覆盖配置）
            on_init();

            // 5. 创建引擎
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

