#include "scene/scene_settings.h"

#include <cmath>
#include <algorithm>

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
        if(!Math::is_finite(wind) || Math::length(wind) > 100)
            return Result<void>::failure("World wind must be finite and <= 100 m/s");
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

    Result<void> LensSettings::validate() const {
        if(!std::isfinite(focus_distance) || focus_distance < .01f || focus_distance > 100000
            || !std::isfinite(focus_range) || focus_range < .01f || focus_range > 100000
            || !std::isfinite(aperture) || aperture < 0 || aperture > 32 || !std::isfinite(shutter)
            || shutter < 0 || shutter > 1 || !std::isfinite(max_blur) || max_blur < 0
            || max_blur > 64)
            return Result<void>::failure("Invalid lens focus, aperture or shutter settings");
        return Result<void>::success();
    }

    Result<void> PathTraceSettings::validate() const {
        if(samples == 0 || samples > 4096 || bounces == 0 || bounces > 8 || max_dimension < 32
            || max_dimension > 2048)
            return Result<void>::failure(
                "路径追踪参数超出范围（采样 1～4096，反弹 1～8，尺寸 32～2048）");
        if(!std::isfinite(history_weight) || history_weight < 0 || history_weight > .98f
            || !std::isfinite(medium_density) || medium_density < 0 || medium_density > 2
            || !Math::is_finite(medium_color) || Math::any_less(medium_color, Math::Vec3(0))
            || Math::any_greater(medium_color, Math::Vec3(1)) || !std::isfinite(medium_anisotropy)
            || std::abs(medium_anisotropy) > .9f)
            return Result<void>::failure("Invalid path trace history or participating medium");
        return Result<void>::success();
    }

    Math::Vec2u PathTraceSettings::image_size(Math::Vec2u output) const {
        const float scale =
            std::min(1.f, float(max_dimension) / std::max({output.x, output.y, 1u}));
        return {std::max(1u, uint32_t(output.x * scale)), std::max(1u, uint32_t(output.y * scale))};
    }

    Result<void> PostProcessSettings::validate() const {
        if(auto valid = path_trace.validate(); !valid)
            return valid;
        if(auto valid = lens.validate(); !valid)
            return valid;
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
