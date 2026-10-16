#pragma once

#include "scene/prefab.h"

namespace Comet {
    class AssetDatabase;
    // 共用于开发源码和已展开的运行产品；不发布 Registry 或修改活动实例。
    [[nodiscard]] COMET_API Result<Prefab> read_prefab_content(
        const AssetDatabase& database, AssetHandle source, const ComponentRegistry& registry);
}
