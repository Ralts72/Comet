#pragma once

#include "common/export.h"
#include "common/result.h"

#include <filesystem>
#include <string_view>

namespace Comet::Ui {
    inline constexpr std::string_view builtin_resource_prefix = "comet://ui/";

    [[nodiscard]] COMET_API const std::filesystem::path& builtin_resource_directory();
    // 只解析引擎 UI 目录中的文件；项目图片仍按文档相对路径引用。
    [[nodiscard]] COMET_API Result<std::filesystem::path> resolve_builtin_resource(
        std::string_view source);
}
