#pragma once

#include "asset/handle.h"
#include "core/math_utils.h"

#include <optional>
#include <string>
#include <vector>

namespace Comet {
    struct ShaderProgramStage {
        AssetHandle source;
        std::string entry = "main";

        bool operator==(const ShaderProgramStage&) const noexcept = default;
    };

    struct ShaderMaterialTexture {
        std::string name;
        std::string display_name;
        bool optional = false;

        bool operator==(const ShaderMaterialTexture&) const = default;
    };

    struct ShaderMaterialScalar {
        std::string name;
        std::string display_name;
        float default_value = 0.0f;
        float min_value = 0.0f;
        float max_value = 1.0f;
        float step = 0.01f;

        bool operator==(const ShaderMaterialScalar&) const = default;
    };

    struct ShaderMaterialVector {
        std::string name;
        std::string display_name;
        Math::Vec4 default_value{0.0f};
        bool color = false;

        bool operator==(const ShaderMaterialVector&) const = default;
    };

    struct ShaderProgramMaterial {
        std::vector<ShaderMaterialTexture> textures;
        std::vector<ShaderMaterialScalar> scalars;
        std::vector<ShaderMaterialVector> vectors;

        bool operator==(const ShaderProgramMaterial&) const = default;
    };

    struct ShaderProgramData {
        ShaderProgramStage vertex;
        ShaderProgramStage fragment;
        std::optional<ShaderProgramMaterial> material;

        bool operator==(const ShaderProgramData&) const noexcept = default;
    };
}
