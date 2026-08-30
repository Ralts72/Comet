#include "asset/material_data.h"

#include <algorithm>

namespace Comet {
    std::vector<AssetHandle> get_asset_dependencies(
        const MaterialData& data) {
        std::vector<AssetHandle> dependencies;
        dependencies.reserve(data.texture_properties.size());
        for(const auto& property: data.texture_properties) {
            dependencies.push_back(property.second);
        }

        std::ranges::sort(dependencies);
        const auto duplicate = std::ranges::unique(dependencies);
        dependencies.erase(duplicate.begin(), duplicate.end());
        return dependencies;
    }
}
