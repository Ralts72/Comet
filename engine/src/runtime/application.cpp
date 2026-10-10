#include "runtime/application.h"
#include "config/config.h"

#include "config/config_loader.h"
#include "diagnostics/logger.h"

#include <iostream>
#include <utility>
#include <vector>

namespace Comet {
    Application::Application(Options options) : m_options(std::move(options)) {}

    Result<void, Error> Application::run(Config config) {
        using RunResult = Result<void, Error>;
        if(m_engine)
            return RunResult::failure({"Application is already started"});
        if(m_options.display_settings) {
            if(auto valid = m_options.display_settings->validate(); !valid)
                return RunResult::failure({valid.error()});
        }
        if(m_options.quality_settings) {
            if(auto valid = m_options.quality_settings->validate(); !valid)
                return RunResult::failure({valid.error()});
        }
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
        if(m_options.display_settings) {
            const auto& display = *m_options.display_settings;
            config.window.width = display.width;
            config.window.height = display.height;
            config.window.mode = display.mode;
            config.window.maximized = false;
            config.vulkan.present_mode = display.vsync ? PresentMode::Fifo : PresentMode::Immediate;
        }
        if(m_options.quality_settings) {
            config.vulkan.msaa_samples =
                static_cast<SampleCount>(m_options.quality_settings->msaa_samples);
            config.render.max_anisotropy = m_options.quality_settings->max_anisotropy;
            config.render.render_scale = m_options.quality_settings->render_scale;
        }
        m_config = std::move(config);
        auto engine = Engine::create(m_config);
        if(!engine) {
            m_diagnostics.reset();
            return RunResult::failure(engine.error());
        }
        m_engine = std::move(engine).value();
        auto result = on_init();
        if(result)
            result = m_engine->run({
                .update = [this](const Engine::FrameContext& frame) { return on_update(frame); },
                .frame_ready =
                    [this](const Engine::FrameContext& frame) { return on_frame_ready(frame); },
                .runtime_input =
                    [this](const Engine::FrameContext& frame) { return on_runtime_input(frame); },
                .runtime_failed = [this](const Error& error) { return on_runtime_error(error); },
            });
        m_engine->prepare_shutdown();
        on_shutdown();
        m_engine.reset();
        m_diagnostics.reset();
        return result;
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
