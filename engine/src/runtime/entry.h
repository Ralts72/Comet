#pragma once

#include "common/error.h"
#include "common/export.h"
#include "common/result.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Comet {
    struct LaunchOptions {
        std::filesystem::path config_file;
        std::string config_profile;
        std::optional<std::filesystem::path> project_path;
    };

    using ApplicationEntry = Result<void, Error> (*)(const LaunchOptions&);

    // App／Editor 共用命令行和退出码；项目加载、配置合成与会话归宿主。
    COMET_API int launch(
        int argc, const char* const* argv, LaunchOptions options, ApplicationEntry run_application);
}
