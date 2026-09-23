#include "asset/data/material_data.h"

#include <algorithm>

namespace Comet {
    std::vector<AssetHandle> get_asset_dependencies(const MaterialData& data) {
        std::vector<AssetHandle> dependencies;
        dependencies.reserve(data.texture_properties.size() + (data.shader_program ? 1 : 0));
        if(data.shader_program)
            dependencies.push_back(data.shader_program);
        for(const auto& property : data.texture_properties) {
            dependencies.push_back(property.second);
        }

        std::ranges::sort(dependencies);
        const auto duplicate = std::ranges::unique(dependencies);
        dependencies.erase(duplicate.begin(), duplicate.end());
        return dependencies;
    }
}
