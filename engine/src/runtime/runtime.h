#pragma once
#include "common/config_loader.h"
#include "common/diagnostics.h"
#include "common/logger.h"
#include "core/engine.h"

namespace Comet {
    class Application {
    public:
        virtual ~Application() = default;

        void start() {
            // 1. 加载配置文件并统一解析运行配置
            m_config = ConfigLoader{}.load();
            m_diagnostics = std::make_unique<Diagnostics>(m_config.diagnostics);

            // 2. 创建引擎
            m_engine = std::make_unique<Engine>(m_config);

            // 3. 用户初始化代码
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
            m_engine.reset();
            m_diagnostics.reset();
        }

        [[nodiscard]] Engine& get_engine() { return *m_engine; }
        [[nodiscard]] const Engine& get_engine() const { return *m_engine; }

        virtual void on_init() = 0;

        virtual void on_update(UpdateContext context) = 0;

        virtual void on_shutdown() = 0;

    private:
        Config m_config;
        std::unique_ptr<Diagnostics> m_diagnostics;
        std::unique_ptr<Engine> m_engine;
    };

    inline void run(Application* app) {
        app->start();
        app->main_loop();
        app->end();
    }
}
