#pragma once

#include "asset/metadata.h"
#include <compare>

namespace Comet {
    struct AssetReference {
        AssetHandle handle;
        AssetType type;
        bool required = true;
        auto operator<=>(const AssetReference&) const = default;
    };
}
