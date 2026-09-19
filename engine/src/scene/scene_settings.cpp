#include "scene/scene_settings.h"

#include <cmath>

namespace Comet {
    Result<void> SceneEnvironment::validate() const {
        for(int channel = 0; channel < 3; ++channel) {
            const auto value = background_color[channel];
            if(!std::isfinite(value) || value < 0.0f || value > MAX_COLOR)
                return Result<void>::failure("background_color must contain finite HDR colors");
        }
        if(!std::isfinite(intensity) || intensity < 0.0f || intensity > MAX_INTENSITY
            || !std::isfinite(lighting_intensity) || lighting_intensity < 0.0f
            || lighting_intensity > MAX_INTENSITY)
            return Result<void>::failure(
                "environment intensity must be finite and between 0 and 64");
        if(!std::isfinite(rotation))
            return Result<void>::failure("environment rotation must be finite");
        return Result<void>::success();
    }

    Result<void> PostProcessSettings::validate() const {
        if(!std::isfinite(exposure) || exposure < 0 || exposure > MAX_EXPOSURE)
            return Result<void>::failure("exposure must be finite and between 0 and 100");
        if(!std::isfinite(bloom_strength) || bloom_strength < 0
            || bloom_strength > MAX_BLOOM_STRENGTH)
            return Result<void>::failure("bloom_strength must be finite and between 0 and 10");
        if(!std::isfinite(bloom_threshold) || bloom_threshold < 0
            || bloom_threshold > MAX_BLOOM_THRESHOLD)
            return Result<void>::failure("bloom_threshold must be finite and between 0 and 65504");
        return Result<void>::success();
    }
}
