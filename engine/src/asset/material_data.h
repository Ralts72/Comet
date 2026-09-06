#pragma once

#include "asset/handle.h"
#include "common/export.h"

#include <compare>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace Comet {
    struct MaterialData {
        std::string template_name;
        std::map<std::string, AssetHandle> texture_properties;
        std::map<std::string, float> scalar_properties;
        std::map<std::string, std::array<float, 4>> vector_properties;

        auto operator<=>(const MaterialData&) const noexcept = default;
    };

    [[nodiscard]] COMET_API std::vector<AssetHandle> get_asset_dependencies(
        const MaterialData& data);
}
