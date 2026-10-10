#pragma once

#include "core/project.h"

#include <vector>

namespace Comet::Ui {
    // 静态文件引用；结果相对 assets，排序去重。共享引擎字体随宿主资源交付。
    [[nodiscard]] COMET_API Result<std::vector<std::filesystem::path>> collect_resource_dependencies(
        const std::filesystem::path& assets, const Project::UiEntry& entry);
}
