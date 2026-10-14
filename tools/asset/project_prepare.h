#pragma once

#include "asset/import/asset_task_types.h"
#include "common/result.h"
#include <filesystem>
#include <span>

namespace Comet {
    class Project;

    // 准备启动场景的依赖产物，不创建窗口或 GPU 资源，也不生成发布包。
    [[nodiscard]] Result<void> prepare_project(
        const Project& project, AssetImportLimits limits = {});

    // 导出启动场景和指定关卡的运行资源；目标必须不存在，不复制源模型或导入缓存。
    // 可执行文件、引擎字体／图标和动态库由宿主打包层交付。
    [[nodiscard]] Result<void> export_project_assets(const Project& project,
        const std::filesystem::path& destination,
        std::span<const std::filesystem::path> scenes = {}, AssetImportLimits limits = {});
}
