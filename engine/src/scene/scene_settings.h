#pragma once

#include "asset/handle.h"
#include "core/math_utils.h"
#include "common/export.h"
#include "common/result.h"

namespace Comet {
    struct COMET_API OutlineSettings {
        bool enabled = true;
        Math::Vec4 color{1, .6f, .1f, 1};
        float width = 3;
        float fill = 0;
        bool through_walls = false;
        [[nodiscard]] Result<void> validate() const {
            if(!Math::is_finite(color) || !std::isfinite(width) || width < 0 || width > 8
                || !std::isfinite(fill) || fill < 0 || fill > 1 || color.a < 0 || color.a > 1)
                return Result<void>::failure("描边宽度应为 0～8 像素，颜色透明度和填充应为 0～1");
            return Result<void>::success();
        }
        [[nodiscard]] bool active() const {
            return enabled && color.a > 0 && (width > 0 || fill > 0) && bool(validate());
        }
        bool operator==(const OutlineSettings&) const = default;
    };

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
        uint32_t realtime_gi_budget = 32;
        Math::Vec3 wind{0};

        [[nodiscard]] Result<void> validate() const;
        bool operator==(const SceneEnvironment&) const = default;
    };

    struct COMET_API ScreenSpaceReflectionSettings {
        bool enabled = false;
        float strength = 1;
        float max_distance = 25;
        float thickness = .15f;
        float max_roughness = .65f;
        [[nodiscard]] bool active() const { return enabled && strength > 0; }
        [[nodiscard]] Result<void> validate() const;
        bool operator==(const ScreenSpaceReflectionSettings&) const = default;
    };

    struct COMET_API LensSettings {
        bool depth_of_field = false;
        float focus_distance = 8;
        float focus_range = 4;
        float aperture = 6; // 最大弥散半径，按源图像像素计。
        bool motion_blur = false;
        float shutter = .5f; // 当前帧位移的曝光比例。
        float max_blur = 24;
        [[nodiscard]] Result<void> validate() const;
        bool operator==(const LensSettings&) const = default;
    };

    struct COMET_API PathTraceSettings {
        uint32_t samples = 256;
        uint32_t samples_per_frame = 1;
        uint32_t bounces = 4;
        uint32_t seed = 0;
        uint64_t reset_generation = 0;
        uint32_t max_dimension = 512;
        bool enabled = false;
        bool dynamic = false;
        bool denoise = false;
        float history_weight = .85f;
        float medium_density = 0;
        Math::Vec3 medium_color{1};
        float medium_anisotropy = 0;
        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] Math::Vec2u image_size(Math::Vec2u output) const;
        bool operator==(const PathTraceSettings&) const = default;
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
        ScreenSpaceReflectionSettings reflections;
        bool weighted_oit = false;
        LensSettings lens;
        PathTraceSettings path_trace;

        [[nodiscard]] bool uses_depth_of_field() const {
            return lens.depth_of_field && lens.aperture > 0;
        }
        [[nodiscard]] bool uses_motion_blur() const {
            return lens.motion_blur && lens.shutter > 0 && lens.max_blur > 0;
        }
        [[nodiscard]] bool uses_lens() const { return uses_depth_of_field() || uses_motion_blur(); }
        [[nodiscard]] bool uses_ao() const { return ao_enabled && ao_strength > 0.0f; }
        [[nodiscard]] bool uses_bloom() const { return bloom_enabled && bloom_strength > 0.0f; }
        [[nodiscard]] bool uses_reflections() const { return reflections.active(); }
        [[nodiscard]] Result<void> validate() const;
        bool operator==(const PostProcessSettings&) const = default;
    };
}
