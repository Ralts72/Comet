#pragma once

#include "config/config.h"
#include "config/display_settings.h"
#include "render/quality_settings.h"
#include "diagnostics/diagnostics.h"
#include "core/engine.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Comet {
    struct LaunchOptions {
        std::filesystem::path config_directory;
        std::string config_profile;
    };

    class COMET_API Application {
    public:
        struct Options {
            std::filesystem::path cache_directory;
            std::filesystem::path log_directory;
            std::optional<OutputMode> output_mode;
            std::optional<Config::Render::SceneOutput> scene_output;
            std::optional<std::string> window_title;
            std::optional<DisplaySettings> display_settings;
            std::optional<QualitySettings> quality_settings;
        };
        explicit Application(Options options = {});
        virtual ~Application() = default;

        [[nodiscard]] Result<void, Error> run(Config config);

        [[nodiscard]] Engine& get_engine() { return *m_engine; }
        [[nodiscard]] const Engine& get_engine() const { return *m_engine; }
        [[nodiscard]] const Config& get_config() const { return m_config; }

        virtual Result<void, Error> on_init() = 0;

        virtual Result<void, Error> on_update(const Engine::FrameContext&) {
            return Result<void, Error>::success();
        }

        // 仅在帧就绪后调用；编辑在随后提取中生效。失败终止生命周期，不重用已获取帧。
        virtual Result<void, Error> on_frame_ready(const Engine::FrameContext&) {
            return Result<void, Error>::success();
        }

        // UI 更新后决定游戏输入归属；渲染延期时也调用，默认不授权。
        virtual std::optional<Input::Frame> on_runtime_input(const Engine::FrameContext&) {
            return std::nullopt;
        }

        // System 已停止且已获取帧已完成；成功表示宿主恢复完成，可进入下一帧。
        virtual Result<void, Error> on_runtime_error(const Error& error) {
            return Result<void, Error>::failure(error);
        }

        // on_init 一旦开始，退出时就会调用；必须能清理部分初始化的状态。
        virtual void on_shutdown() = 0;

    private:
        Options m_options;
        Config m_config;
        std::unique_ptr<Diagnostics> m_diagnostics;
        std::unique_ptr<Engine> m_engine;
    };

    COMET_API int run(Application* app, const LaunchOptions& options);
}
