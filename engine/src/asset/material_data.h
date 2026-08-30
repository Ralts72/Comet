#pragma once

#include "asset/handle.h"

#include <compare>
#include <map>
#include <string>

namespace Comet {
    struct MaterialData {
        std::string template_name;
        std::map<std::string, AssetHandle> texture_properties;

        auto operator<=>(const MaterialData&) const noexcept = default;
    };
}
