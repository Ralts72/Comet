#pragma once

#include "asset/handle.h"
#include "common/export.h"
#include "core/math_utils.h"

#include <map>
#include <string>
#include <vector>

namespace Comet {
    struct MaterialData {
        std::string template_name;
        std::map<std::string, AssetHandle> texture_properties;
        std::map<std::string, float> scalar_properties;
        std::map<std::string, Math::Vec4> vector_properties;

        bool operator==(const MaterialData&) const noexcept = default;
    };

    [[nodiscard]] COMET_API std::vector<AssetHandle> get_asset_dependencies(
        const MaterialData& data);
}
