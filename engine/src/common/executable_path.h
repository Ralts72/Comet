#pragma once

#include "common/export.h"
#include <filesystem>

namespace Comet {
    // 不依赖当前工作目录；无法取得进程路径时返回空。
    [[nodiscard]] COMET_API std::filesystem::path executable_directory();
    // macOS bundle 的 Contents/Resources，其他宿主的可执行文件同级 resources/。
    [[nodiscard]] COMET_API std::filesystem::path executable_resource_directory();
}
