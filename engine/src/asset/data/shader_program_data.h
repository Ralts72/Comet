#pragma once

#include "asset/handle.h"

#include <string>

namespace Comet {
    struct ShaderProgramStage {
        AssetHandle source;
        std::string entry = "main";

        bool operator==(const ShaderProgramStage&) const noexcept = default;
    };

    struct ShaderProgramData {
        ShaderProgramStage vertex;
        ShaderProgramStage fragment;

        bool operator==(const ShaderProgramData&) const noexcept = default;
    };
}
