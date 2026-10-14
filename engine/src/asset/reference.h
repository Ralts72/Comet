#pragma once

#include "asset/metadata.h"
#include <compare>

namespace Comet {
    struct AssetReference {
        AssetHandle handle;
        AssetType type;
        bool required = true;
        // CPU 网格消费者不要求渲染资源驻留。
        bool cpu_only = false;
        auto operator<=>(const AssetReference&) const = default;
    };
}
