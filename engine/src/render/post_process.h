#pragma once

#include "common/export.h"
#include "common/result.h"

namespace Comet {
    struct COMET_API PostProcessSettings {
        float exposure = 1.0f;
        float bloom_strength = 0.0f;
        float bloom_threshold = 1.0f;

        [[nodiscard]] bool bloom_enabled() const { return bloom_strength > 0.0f; }
        [[nodiscard]] Result<void> validate() const;
    };
}
