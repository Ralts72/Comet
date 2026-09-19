#pragma once

#include "asset/handle.h"
#include "core/math_utils.h"
#include "common/export.h"
#include "common/result.h"

namespace Comet {
    struct COMET_API SceneEnvironment {
        static constexpr float MAX_INTENSITY = 64.0f;
        static constexpr float MAX_COLOR = 65504.0f;
        AssetHandle asset;
        bool background = false;
        float intensity = 1.0f;
        float rotation = 0.0f;
        bool lighting = false;
        float lighting_intensity = 1.0f;
        Math::Vec3 background_color{0.0f};

        [[nodiscard]] Result<void> validate() const;
        bool operator==(const SceneEnvironment&) const = default;
    };

    struct COMET_API PostProcessSettings {
        static constexpr float MAX_EXPOSURE = 100.0f;
        static constexpr float MAX_BLOOM_STRENGTH = 10.0f;
        static constexpr float MAX_BLOOM_THRESHOLD = 65504.0f;

        float exposure = 1.0f;
        bool bloom_enabled = false;
        float bloom_strength = 0.15f;
        float bloom_threshold = 1.0f;

        [[nodiscard]] bool uses_bloom() const { return bloom_enabled && bloom_strength > 0.0f; }
        [[nodiscard]] Result<void> validate() const;
        bool operator==(const PostProcessSettings&) const = default;
    };
}
