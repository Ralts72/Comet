#pragma once

#include "asset/handle.h"
#include "common/export.h"

#include <compare>
#include <map>
#include <string>
#include <vector>

namespace Comet {
    struct MaterialData {
        std::string template_name;
        std::map<std::string, AssetHandle> texture_properties;

        auto operator<=>(const MaterialData&) const noexcept = default;
    };

    [[nodiscard]] COMET_API std::vector<AssetHandle> get_asset_dependencies(
        const MaterialData& data);
}
