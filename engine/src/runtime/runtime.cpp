#include "runtime/runtime.h"

#include "config/config_loader.h"

#include <exception>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Comet {
    void Application::run(Config config) {
        if(m_engine)
            throw std::logic_error("Application is already started");
        try {
            m_diagnostics = std::make_unique<Diagnostics>(config.diagnostics);
            m_engine = std::make_unique<Engine>(config);
            m_shutdown_required = true;
            on_init();
            m_engine->register_update_callback(
                [this](const UpdateContext dt) { on_update(dt); });
            m_engine->on_update();
            end();
        } catch(...) {
            if(!m_engine)
                m_diagnostics.reset();
            end_after_failure();
            throw;
        }
    }

    void Application::end_after_failure() noexcept {
        try {
            end();
        } catch(const std::exception& error) {
            std::fprintf(stderr, "Application cleanup also failed: %s\n", error.what());
        } catch(...) {
            std::fputs("Application cleanup also failed\n", stderr);
        }
    }

    void Application::end() {
        if(!std::exchange(m_shutdown_required, false))
            return;
        // 钩子失败时保留 Engine，让派生类剩余成员先析构，避免悬空 GPU owner。
        on_shutdown();
        m_engine.reset();
        m_diagnostics.reset();
    }

    int run(Application* app, const LaunchOptions& options) {
        const auto& directory = options.config_directory;
        Config config = ConfigLoader{}.load(
            std::vector<std::string>{(directory / "common.yaml").string(),
                (directory / "profiles" / (options.config_profile + ".yaml")).string()});
        app->run(std::move(config));
        return 0;
    }
}
