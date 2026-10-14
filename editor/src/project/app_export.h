#pragma once

#include "common/result.h"
#include <filesystem>
#include <vector>

namespace CometEditor {
    struct AppExportRequest {
        std::filesystem::path project;
        std::filesystem::path build;
        std::filesystem::path destination;
        std::vector<std::filesystem::path> scenes;
    };

    // 在后台调用；使用参数数组启动打包工具，不经过 shell。
    Comet::Result<void> export_app(const AppExportRequest& request);
}
