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
        struct Options {
            std::filesystem::path cache_directory;
            std::filesystem::path log_directory;
            std::optional<OutputMode> output_mode;
            std::optional<Config::Render::SceneOutput> scene_output;
        };
        explicit Application(Options options = {});
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

        Options m_options;
        std::unique_ptr<Diagnostics> m_diagnostics;
        std::unique_ptr<Engine> m_engine;
        bool m_shutdown_required = false;
    };

    COMET_API int run(Application* app, const LaunchOptions& options);
}
