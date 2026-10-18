#pragma once

#include "asset/content_fwd.h"
#include "asset/reference.h"
#include "common/error.h"
#include "common/result.h"

#include <functional>
#include <memory>
#include <vector>

namespace Comet {
    class Scene;
    class Script;
    class ComponentRegistry;

    struct SceneDependencySources {
        std::function<Result<std::shared_ptr<Script>, Error>(AssetHandle)> script;
        std::function<Result<std::shared_ptr<Prefab>, Error>(AssetHandle)> prefab;
        std::function<Result<std::shared_ptr<GameData>, Error>(AssetHandle)> data;
    };
    struct SceneDependencyOptions {
        bool allow_missing = false;
        bool include_streamed = true;
    };

    // 运行需求和导出共用内容闭包；来源读取由宿主提供，不修改活动场景或发布资产。
    [[nodiscard]] COMET_API Result<std::vector<AssetReference>, Error> collect_scene_dependencies(
        Scene& scene, const ComponentRegistry& components, const SceneDependencySources& sources,
        SceneDependencyOptions options = {});
}
