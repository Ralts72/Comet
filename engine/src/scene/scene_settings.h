#pragma once

#include "asset/handle.h"
#include "core/math_utils.h"
#include "common/export.h"
#include "common/result.h"

namespace Comet {
    enum class LightType { Directional = 0, Point = 1, Spot = 2 };

    struct COMET_API VolumetricFogSettings {
        bool enabled = false;
        Math::Vec3 color{.6f, .7f, .8f};
        float density = .03f;
        float base_height = 0;
        float height_falloff = .15f;
        float anisotropy = .2f;
        float max_distance = 80;
        float ambient = .1f;
        [[nodiscard]] bool active() const { return enabled && density > 0; }
        [[nodiscard]] Result<void> validate() const;
        bool operator==(const VolumetricFogSettings&) const = default;
    };

    struct COMET_API SceneEnvironment {
        static constexpr float MAX_INTENSITY = 64.0f;
        static constexpr float MAX_COLOR = 65504.0f;
        static constexpr float MAX_FOG_DISTANCE = 100000.0f;
        AssetHandle asset;
        bool background = false;
        float intensity = 1.0f;
        float rotation = 0.0f;
        bool lighting = false;
        float lighting_intensity = 1.0f;
        Math::Vec3 background_color{0.0f};
        bool fog_enabled = false;
        Math::Vec3 fog_color{0.5f};
        float fog_start = 10.0f;
        float fog_end = 100.0f;
        VolumetricFogSettings volumetric_fog;

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
        bool ao_enabled = false;
        float ao_radius = 0.5f;
        float ao_strength = 1.0f;

        [[nodiscard]] bool uses_ao() const { return ao_enabled && ao_strength > 0.0f; }
        [[nodiscard]] bool uses_bloom() const { return bloom_enabled && bloom_strength > 0.0f; }
        [[nodiscard]] Result<void> validate() const;
        bool operator==(const PostProcessSettings&) const = default;
    };
}
