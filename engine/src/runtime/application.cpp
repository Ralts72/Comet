#include "runtime/application.h"
#include "config/config.h"

#include "config/config_loader.h"
#include "diagnostics/logger.h"

#include <cstdio>
#include <iostream>
#include <utility>
#include <vector>

namespace Comet {
    Application::Application(std::filesystem::path cache_directory,
        std::filesystem::path log_directory, std::optional<OutputMode> output_mode_override,
        Config::Render::SceneOutput scene_output)
        : m_cache_directory(std::move(cache_directory)), m_log_directory(std::move(log_directory)),
          m_output_mode_override(output_mode_override), m_scene_output(scene_output) {}

    Result<void, Error> Application::run(Config config) {
        using RunResult = Result<void, Error>;
        if(m_engine)
            return RunResult::failure({"Application is already started"});
        if(!m_cache_directory.empty())
            config.vulkan.pipeline_cache_directory = m_cache_directory / "vulkan";
        if(!m_log_directory.empty())
            config.diagnostics.log.directory = m_log_directory;
        m_diagnostics = std::make_unique<Diagnostics>(config.diagnostics);
        if(m_output_mode_override) {
            if(config.render.output_mode != *m_output_mode_override)
                LOG_INFO("Application overrides the configured output mode (editor uses SDR)");
            config.render.output_mode = *m_output_mode_override;
        }
        config.render.scene_output = m_scene_output;
        auto engine = Engine::create(config);
        if(!engine) {
            m_diagnostics.reset();
            return RunResult::failure(engine.error());
        }
        m_engine = std::move(engine).value();
        m_shutdown_required = true;
        auto result = on_init();
        if(result)
            result = m_engine->run([this](const UpdateContext dt) { return on_update(dt); },
                [this] { return on_frame_ready(); });
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
