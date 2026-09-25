#include "runtime/application.h"
#include "config/config.h"

#include "config/config_loader.h"
#include "diagnostics/logger.h"

#include <cstdio>
#include <iostream>
#include <utility>
#include <vector>

namespace Comet {
    Application::Application(Options options) : m_options(std::move(options)) {}

    Result<void, Error> Application::run(Config config) {
        using RunResult = Result<void, Error>;
        if(m_engine)
            return RunResult::failure({"Application is already started"});
        if(!m_options.cache_directory.empty())
            config.vulkan.pipeline_cache_directory = m_options.cache_directory / "vulkan";
        if(!m_options.log_directory.empty())
            config.diagnostics.log.directory = m_options.log_directory;
        m_diagnostics = std::make_unique<Diagnostics>(config.diagnostics);
        if(m_options.output_mode) {
            if(config.render.output_mode != *m_options.output_mode)
                LOG_INFO("Application overrides the configured output mode");
            config.render.output_mode = *m_options.output_mode;
        }
        if(m_options.scene_output)
            config.render.scene_output = *m_options.scene_output;
        if(m_options.window_title)
            config.window.title = *m_options.window_title;
        m_config = std::move(config);
        auto engine = Engine::create(m_config);
        if(!engine) {
            m_diagnostics.reset();
            return RunResult::failure(engine.error());
        }
        m_engine = std::move(engine).value();
        m_shutdown_required = true;
        auto result = on_init();
        if(result)
            result = m_engine->run([this](Engine::FrameContext& frame) { return on_update(frame); },
                [this](Engine::FrameContext& frame) { return on_frame_ready(frame); },
                [this](const Error& error) { return on_runtime_error(error); });
        auto cleanup = end();
        if(!cleanup) {
            if(result)
                return cleanup;
            std::fprintf(
                stderr, "Application cleanup also failed: %s\n", cleanup.error().message.c_str());
        }
        return result;
    }

    Result<void, Error> Application::end() {
        using RunResult = Result<void, Error>;
        if(!std::exchange(m_shutdown_required, false))
            return RunResult::success();
        m_engine->prepare_shutdown();
        // 钩子失败时保留 Engine，让派生类剩余成员先析构，避免悬空 GPU owner。
        if(auto result = on_shutdown(); !result)
            return result;
        m_engine.reset();
        m_diagnostics.reset();
        return RunResult::success();
    }

    int run(Application* app, const LaunchOptions& options) {
        const auto& directory = options.config_directory;
        auto config =
            ConfigLoader{}.load(std::vector<std::string>{(directory / "common.yaml").string(),
                (directory / "profiles" / (options.config_profile + ".yaml")).string()});
        if(!config) {
            std::cerr << "Application failed: " << config.error() << '\n';
            return 1;
        }
        if(auto result = app->run(std::move(config).value()); !result) {
            std::cerr << "Application failed: " << result.error().message << '\n';
            return 1;
        }
        return 0;
    }
}
