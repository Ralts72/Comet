#pragma once

#include "asset/handle.h"
#include "common/export.h"
#include "core/math_utils.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Comet {
    enum class AlphaMode : uint8_t { Opaque, Mask, Blend };

    struct MaterialSurface {
        AlphaMode alpha_mode = AlphaMode::Opaque;
        float alpha_cutoff = 0.5f;
        bool double_sided = false;

        [[nodiscard]] bool valid() const {
            return alpha_mode <= AlphaMode::Blend && std::isfinite(alpha_cutoff)
                   && alpha_cutoff >= 0 && alpha_cutoff <= 1;
        }
        bool operator==(const MaterialSurface&) const = default;
    };

    struct MaterialData {
        std::string template_name;
        AssetHandle shader_program;
        std::map<std::string, AssetHandle> texture_properties;
        std::map<std::string, float> scalar_properties;
        std::map<std::string, Math::Vec4> vector_properties;

        MaterialSurface surface;

        bool operator==(const MaterialData&) const noexcept = default;
    };

    [[nodiscard]] COMET_API std::vector<AssetHandle> get_asset_dependencies(
        const MaterialData& data);
}
