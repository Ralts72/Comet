#include "render/lighting.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Comet {
    static_assert(sizeof(LightingData::Light) == 64);
    static_assert(offsetof(LightingData::Light, type) == 12);
    static_assert(offsetof(LightingData::Light, direction) == 16);
    static_assert(offsetof(LightingData::Light, range) == 28);
    static_assert(offsetof(LightingData::Light, color) == 32);
    static_assert(offsetof(LightingData::Light, intensity) == 44);
    static_assert(offsetof(LightingData::Light, inner_cone_cos) == 48);
    static_assert(offsetof(LightingData::Light, outer_cone_cos) == 52);
    static_assert(offsetof(LightingData::Light, casts_shadow) == 56);
    static_assert(offsetof(LightingData, light_count) == LightingData::MAX_LIGHTS * 64);
    static_assert(offsetof(LightingData, excess_lights) == LightingData::MAX_LIGHTS * 64 + 4);
    static_assert(offsetof(LightingData, invalid_lights) == LightingData::MAX_LIGHTS * 64 + 8);
    static_assert(
        offsetof(LightingData, shadow_view_projection) == LightingData::MAX_LIGHTS * 64 + 16);
    static_assert(offsetof(LightingData, shadow_light_index) == LightingData::MAX_LIGHTS * 64 + 80);
    static_assert(offsetof(LightingData, shadow_depth_bias) == LightingData::MAX_LIGHTS * 64 + 84);
    static_assert(offsetof(LightingData, shadow_texel_size) == LightingData::MAX_LIGHTS * 64 + 88);
    static_assert(sizeof(LightingData) == LightingData::MAX_LIGHTS * 64 + 112);

    static bool is_valid_light(const RenderLight& light) {
        if(light.type != LightType::Directional && light.type != LightType::Point
            && light.type != LightType::Spot)
            return false;
        if(!Math::is_finite(light.color) || light.color.x < 0 || light.color.y < 0
            || light.color.z < 0 || light.color.x > 1 || light.color.y > 1 || light.color.z > 1)
            return false;
        if(!std::isfinite(light.intensity) || light.intensity < 0 || light.intensity > 10000)
            return false;

        if(light.type != LightType::Directional) {
            if(!Math::is_finite(light.position) || !std::isfinite(light.range) || light.range <= 0
                || light.range > 1000000)
                return false;
        }
        if(light.type != LightType::Point) {
            const float direction_length_squared = Math::dot(light.direction, light.direction);
            if(!Math::is_finite(light.direction) || !std::isfinite(direction_length_squared)
                || direction_length_squared < 1e-12f)
                return false;
        }
        if(light.type == LightType::Spot) {
            if(!std::isfinite(light.inner_angle) || !std::isfinite(light.outer_angle)
                || light.inner_angle < 0 || light.inner_angle >= light.outer_angle
                || light.outer_angle >= 90)
                return false;
        }
        return true;
    }

    LightingData LightingData::prepare(const std::span<const RenderLight> input) {
        LightingData result;
        std::vector<const RenderLight*> sorted;
        for(const auto& light : input) {
            if(!is_valid_light(light)) {
                ++result.invalid_lights;
                continue;
            }
            sorted.push_back(&light);
        }
        std::stable_sort(sorted.begin(), sorted.end(),
            [](const auto* a, const auto* b) { return a->entity_id < b->entity_id; });
        result.light_count = static_cast<float>(std::min(sorted.size(), size_t(MAX_LIGHTS)));
        result.excess_lights = static_cast<float>(sorted.size()) - result.light_count;
        for(size_t index = 0; index < static_cast<size_t>(result.light_count); ++index) {
            const auto& light = *sorted[index];
            auto& packed = result.lights[index];
            if(light.type != LightType::Directional) {
                packed.position = light.position;
                packed.range = light.range;
            }
            packed.type = static_cast<float>(light.type);
            packed.direction = {0, 0, -1};
            if(light.type != LightType::Point)
                packed.direction = Math::normalize(light.direction);
            packed.color = light.color;
            packed.intensity = light.intensity;
            if(light.type == LightType::Spot) {
                packed.inner_cone_cos = std::cos(Math::radians(light.inner_angle));
                packed.outer_cone_cos = std::cos(Math::radians(light.outer_angle));
            }
            packed.casts_shadow = light.casts_shadow ? 1.0f : 0.0f;
        }
        return result;
    }

    void LightingData::prepare_shadow(const BoundingBox& world_bounds, const uint32_t resolution) {
        shadow_view_projection = Math::Mat4(1);
        shadow_light_index = -1;
        shadow_depth_bias = 0;
        shadow_texel_size = 0;
        shadow_reserved = 0;
        if(!world_bounds.is_valid() || resolution == 0 || !std::isfinite(light_count))
            return;
        const float radius = Math::length(world_bounds.size()) * 0.5f;
        if(!std::isfinite(radius) || radius < 1e-5f)
            return;
        const auto count = uint32_t(std::clamp(light_count, 0.0f, float(MAX_LIGHTS)));
        for(uint32_t index = 0; index < count; ++index) {
            const auto& light = lights[index];
            if(light.type != float(LightType::Directional) || light.casts_shadow == 0
                || light.intensity <= 0)
                continue;
            const Math::Vec3 direction = light.direction;
            const auto center = world_bounds.center();
            const float padding = std::max(radius * 0.05f, 0.01f);
            Math::Vec3 up{0, 1, 0};
            if(std::abs(direction.y) > 0.95f)
                up = {1, 0, 0};
            const auto view =
                Math::look_at(center - direction * (radius + 2 * padding), center, up);
            const auto box = transform_box(world_bounds, view);
            if(!box)
                return;
            const auto projection = Math::ortho(box->minimum.x - padding, box->maximum.x + padding,
                box->minimum.y - padding, box->maximum.y + padding, -box->maximum.z - padding,
                -box->minimum.z + padding);
            const auto matrix = projection * view;
            for(int column = 0; column < 4; ++column)
                if(!Math::is_finite(matrix[column]))
                    return;
            shadow_view_projection = matrix;
            shadow_light_index = float(index);
            shadow_depth_bias = 0.0005f;
            shadow_texel_size = 1.0f / float(resolution);
            return;
        }
    }
}
