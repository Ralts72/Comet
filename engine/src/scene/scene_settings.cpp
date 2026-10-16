#include "scene/scene_settings.h"

#include <cmath>

namespace Comet {
    Result<void> SceneEnvironment::validate() const {
        for(int channel = 0; channel < 3; ++channel) {
            const auto value = background_color[channel];
            if(!std::isfinite(value) || value < 0.0f || value > MAX_COLOR)
                return Result<void>::failure("background_color must contain finite HDR colors");
        }
        for(int channel = 0; channel < 3; ++channel) {
            const auto value = fog_color[channel];
            if(!std::isfinite(value) || value < 0.0f || value > MAX_COLOR)
                return Result<void>::failure("fog_color must contain finite HDR colors");
        }
        if(!std::isfinite(fog_start) || !std::isfinite(fog_end) || fog_start < 0
            || fog_end <= fog_start || fog_end > MAX_FOG_DISTANCE
            || !std::isfinite(1.0f / (fog_end - fog_start)))
            return Result<void>::failure(
                "fog distances require 0 <= fog_start < fog_end <= 100000 and a finite inverse range");
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
        if(!std::isfinite(ao_radius) || ao_radius < 0.01f || ao_radius > 10)
            return Result<void>::failure("ao_radius must be finite and between 0.01 and 10");
        if(!std::isfinite(ao_strength) || ao_strength < 0 || ao_strength > 2)
            return Result<void>::failure("ao_strength must be finite and between 0 and 2");
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
