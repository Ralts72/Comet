#pragma once

#include "asset/import/asset_task_types.h"
#include "common/result.h"

namespace Comet {
    class Project;

    // 准备启动场景的依赖产物，不创建窗口或 GPU 资源，也不生成发布包。
    [[nodiscard]] Result<void> prepare_project(
        const Project& project, AssetImportLimits limits = {});
}
