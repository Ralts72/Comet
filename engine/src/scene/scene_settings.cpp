#include "scene/scene_settings.h"

#include <cmath>

namespace Comet {
    Result<void> VolumetricFogSettings::validate() const {
        for(int channel = 0; channel < 3; ++channel)
            if(!std::isfinite(color[channel]) || color[channel] < 0 || color[channel] > 1)
                return Result<void>::failure("volumetric fog color must be between 0 and 1");
        if(!std::isfinite(density) || density < 0 || density > 2 || !std::isfinite(base_height)
            || std::abs(base_height) > 100000 || !std::isfinite(height_falloff)
            || height_falloff < 0 || height_falloff > 10 || !std::isfinite(anisotropy)
            || std::abs(anisotropy) > .9f || !std::isfinite(max_distance) || max_distance < 1
            || max_distance > 1000 || !std::isfinite(ambient) || ambient < 0 || ambient > 16)
            return Result<void>::failure("Invalid volumetric fog density, height, phase or range");
        return Result<void>::success();
    }

    Result<void> SceneEnvironment::validate() const {
        if(!realtime_gi_budget || realtime_gi_budget > 16384)
            return Result<void>::failure("实时 GI 总预算须为 1～16384 个探针");
        if(auto valid = volumetric_fog.validate(); !valid)
            return valid;
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

    Result<void> ScreenSpaceReflectionSettings::validate() const {
        if(!std::isfinite(strength) || strength < 0 || strength > 1 || !std::isfinite(max_distance)
            || max_distance < .1f || max_distance > 200 || !std::isfinite(thickness)
            || thickness < .001f || thickness > 2 || !std::isfinite(max_roughness)
            || max_roughness < .05f || max_roughness > 1)
            return Result<void>::failure("Invalid SSR strength, distance, thickness or roughness");
        return Result<void>::success();
    }

    Result<void> PostProcessSettings::validate() const {
        if(auto valid = reflections.validate(); !valid)
            return valid;
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
