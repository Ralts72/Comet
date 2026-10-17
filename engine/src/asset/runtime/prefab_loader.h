#pragma once

#include "scene/prefab.h"
#include "scene/scene_serializer.h"
#include "asset/import/asset_task_types.h"

namespace Comet {
    class AssetDatabase;
    // 共用于开发源码和已展开的运行产品；不发布 Registry 或修改活动实例。
    [[nodiscard]] COMET_API Result<Prefab> read_prefab_content(
        const AssetDatabase& database, AssetHandle source, const ComponentRegistry& registry);
    // 离线工具持有索引并接纳同步导入；活动宿主由 AssetManager 管理发布。
    [[nodiscard]] COMET_API SceneSerializer::Sources project_scene_sources(
        AssetDatabase& database, const ComponentRegistry& registry, AssetImportLimits limits = {});
}
