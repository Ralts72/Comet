#pragma once

#include "config/config.h"
#include "diagnostics/diagnostics.h"
#include "core/engine.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Comet {
    struct LaunchOptions {
        std::filesystem::path config_directory;
        std::string config_profile;
    };

    class COMET_API Application {
    public:
        virtual ~Application() = default;

        void run(Config config);

        [[nodiscard]] Engine& get_engine() { return *m_engine; }
        [[nodiscard]] const Engine& get_engine() const { return *m_engine; }

        virtual void on_init() = 0;

        virtual void on_update(UpdateContext context) = 0;

        // on_init 一旦开始，退出时就会调用；必须能清理部分初始化的状态。
        virtual void on_shutdown() = 0;

    private:
        void end();
        void end_after_failure() noexcept;

        std::unique_ptr<Diagnostics> m_diagnostics;
        std::unique_ptr<Engine> m_engine;
        bool m_shutdown_required = false;
    };

    COMET_API int run(Application* app, const LaunchOptions& options);
}
