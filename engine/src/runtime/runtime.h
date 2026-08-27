#pragma once

#include "common/config.h"
#include "common/config_loader.h"
#include "common/diagnostics.h"
#include "core/engine.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Comet {
    struct LaunchOptions {
        std::filesystem::path config_directory;
        std::string config_profile;
    };

    class Application {
    public:
        virtual ~Application() = default;

        void start(Config config) {
            // 1. 根据入口提供的配置初始化运行期诊断
            m_diagnostics = std::make_unique<Diagnostics>(config.diagnostics);

            // 2. 创建引擎
            m_engine = std::make_unique<Engine>(std::move(config));

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
        std::unique_ptr<Diagnostics> m_diagnostics;
        std::unique_ptr<Engine> m_engine;
    };

    inline int run(Application* app, const LaunchOptions& options) {
        const auto& config_directory = options.config_directory;
        Config config = ConfigLoader{}.load(std::vector<std::string>{
            (config_directory / "common.yaml").string(),
            (config_directory / "profiles" / (options.config_profile + ".yaml")).string()
        });

        app->start(std::move(config));
        app->main_loop();
        app->end();
        return 0;
    }
}
