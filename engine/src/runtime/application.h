#pragma once

#include "config/config.h"
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
        explicit Application(std::filesystem::path cache_directory = {},
            std::filesystem::path log_directory = {},
            std::optional<OutputMode> output_mode_override = {},
            Config::Render::SceneOutput scene_output = Config::Render::SceneOutput::Presentation);
        virtual ~Application() = default;

        [[nodiscard]] Result<void, Error> run(Config config);

        [[nodiscard]] Engine& get_engine() { return *m_engine; }
        [[nodiscard]] const Engine& get_engine() const { return *m_engine; }

        virtual Result<void, Error> on_init() = 0;

        virtual Result<void, Error> on_update(UpdateContext context) {
            return Result<void, Error>::success();
        }

        // 仅在帧就绪后调用；编辑在随后提取中生效。失败终止生命周期，不重用已获取帧。
        virtual Result<void, Error> on_frame_ready() { return Result<void, Error>::success(); }

        // on_init 一旦开始，退出时就会调用；必须能清理部分初始化的状态。
        virtual Result<void, Error> on_shutdown() = 0;

    private:
        [[nodiscard]] Result<void, Error> end();

        std::filesystem::path m_cache_directory;
        std::filesystem::path m_log_directory;
        std::optional<OutputMode> m_output_mode_override;
        Config::Render::SceneOutput m_scene_output;
        std::unique_ptr<Diagnostics> m_diagnostics;
        std::unique_ptr<Engine> m_engine;
        bool m_shutdown_required = false;
    };

    COMET_API int run(Application* app, const LaunchOptions& options);
}
