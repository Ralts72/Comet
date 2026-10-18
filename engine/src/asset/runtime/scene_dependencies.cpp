#include "asset/runtime/scene_dependencies.h"

#include "asset/data/game_data.h"
#include "asset/script.h"
#include "scene/component_registry.h"
#include "scene/prefab.h"
#include "scene/script_component.h"

#include <algorithm>
#include <unordered_set>

namespace Comet {
    Result<std::vector<AssetReference>, Error> collect_scene_dependencies(Scene& scene,
        const ComponentRegistry& components, const SceneDependencySources& sources,
        SceneDependencyOptions options) {
        using Collected = Result<std::vector<AssetReference>, Error>;
        std::vector<AssetReference> references;
        const auto append = [&](Scene& content) -> Result<void, Error> {
            const auto direct = components.collect_asset_references(
                content, options.include_streamed ? ComponentRegistry::ReferenceScope::Runtime
                                                  : ComponentRegistry::ReferenceScope::Resident);
            references.insert(references.end(), direct.begin(), direct.end());
            for(const auto entity : content.get_entities()) {
                if(!entity.has_component<ScriptComponent>())
                    continue;
                const auto& binding = entity.get_component<ScriptComponent>();
                if(!binding.asset)
                    continue;
                auto script = sources.script(binding.asset);
                if(!script) {
                    if(options.allow_missing)
                        continue;
                    return Result<void, Error>::failure(script.error());
                }
                const auto inputs = script.value()->asset_references(binding.parameters);
                if(!inputs) {
                    if(options.allow_missing)
                        continue;
                    return Result<void, Error>::failure(inputs.error());
                }
                references.insert(references.end(), inputs.value().begin(), inputs.value().end());
            }
            return Result<void, Error>::success();
        };
        if(auto added = append(scene); !added)
            return Collected::failure(added.error());
        std::unordered_set<AssetHandle> expanded;
        for(std::size_t index = 0; index < references.size(); ++index) {
            const auto reference = references[index];
            if(reference.type != AssetType::Prefab && reference.type != AssetType::Data)
                continue;
            if(!expanded.insert(reference.handle).second)
                continue;
            if(reference.type == AssetType::Data) {
                auto data = sources.data(reference.handle);
                if(!data) {
                    if(options.allow_missing)
                        continue;
                    return Collected::failure(data.error());
                }
                const auto& inputs = data.value()->references();
                references.insert(references.end(), inputs.begin(), inputs.end());
                continue;
            }
            const auto prefab = sources.prefab(reference.handle);
            if(!prefab) {
                if(options.allow_missing)
                    continue;
                return Collected::failure(prefab.error());
            }
            Scene content;
            if(auto instance = prefab.value()->instantiate(content, components); !instance)
                return Collected::failure({instance.error()});
            if(auto added = append(content); !added)
                return Collected::failure(added.error());
        }
        std::ranges::sort(references);
        references.erase(std::unique(references.begin(), references.end()), references.end());
        return Collected::success(std::move(references));
    }

}
