#pragma once

#include "asset/database.h"
#include "asset/data/game_data.h"
#include "asset/registry.h"
#include "asset/runtime/prefab_loader.h"
#include "asset/script.h"
#include "scene/scene_serializer.h"

namespace Comet::Tests {
    inline Result<std::unique_ptr<Scene>> load_demo_scene(
        const ComponentRegistry& components, const std::filesystem::path& relative) {
        const ProjectPaths paths(COMET_SAMPLE_PROJECT_DIRECTORY);
        AssetDatabase database(paths);
        const auto scan = database.scan();
        if(!scan.succeeded())
            return Result<std::unique_ptr<Scene>>::failure("Cannot index sample project assets");
        return SceneSerializer(components, project_scene_sources(database, components))
            .load((paths.assets() / relative).string());
    }

    // 领域测试手动准备资源；旅程与展示关共用的玩家脚本也须进入同一运行域。
    inline Result<void, Error> prepare_demo_journey(AssetRegistry& assets) {
        const auto root = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets";
        for(const auto& [handle, path] : {std::pair{6729301842000001201ull, "scripts/journey.lua"},
                std::pair{6729301842000000023ull, "scripts/character_controls.lua"},
                std::pair{13857136248767905805ull, "scripts/character_feedback.lua"}}) {
            if(assets.contains(AssetHandle(handle)))
                continue;
            auto script = Script::load(root / path);
            if(!script)
                return Result<void, Error>::failure(script.error());
            if(auto registered = assets.register_asset(AssetHandle(handle), script.value());
                !registered)
                return Result<void, Error>::failure({"Cannot register journey script"});
        }
        auto catalog = GameData::load(root / "data/journey.data.json");
        if(!catalog)
            return Result<void, Error>::failure({catalog.error()});
        if(!assets.register_asset(AssetHandle(6729301842000001200ull),
               std::make_shared<GameData>(std::move(catalog).value())))
            return Result<void, Error>::failure({"Cannot register journey catalog"});
        return Result<void, Error>::success();
    }
}
