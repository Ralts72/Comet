#pragma once

#include "common/export.h"
#include "common/result.h"

namespace Comet {
    struct COMET_API PostProcessSettings {
        float exposure = 1.0f;
        bool bloom_enabled = false;
        float bloom_strength = 0.15f;
        float bloom_threshold = 1.0f;

        [[nodiscard]] bool uses_bloom() const { return bloom_enabled && bloom_strength > 0.0f; }
        [[nodiscard]] Result<void> validate() const;
        bool operator==(const PostProcessSettings&) const = default;
    };
}
