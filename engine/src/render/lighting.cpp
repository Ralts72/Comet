#include "render/lighting.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Comet {
    LightingData LightingData::prepare(const std::span<const RenderLight> input) {
        static_assert(sizeof(Light) == 64);
        static_assert(offsetof(LightingData, counts) == MAX_LIGHTS * 64);
        static_assert(sizeof(LightingData) == MAX_LIGHTS * 64 + 16);
        LightingData result;
        std::vector<const RenderLight*> sorted;
        for(const auto& light : input) {
            const bool directional = light.type == LightType::Directional;
            const bool point = light.type == LightType::Point;
            const bool spot = light.type == LightType::Spot;
            const bool color_valid = Math::is_finite(light.color) && light.color.x >= 0
                                     && light.color.y >= 0 && light.color.z >= 0
                                     && light.color.x <= 1 && light.color.y <= 1
                                     && light.color.z <= 1;
            if((!directional && !point && !spot) || !color_valid
                || !std::isfinite(light.intensity) || light.intensity < 0
                || light.intensity > 10000
                || (!directional
                    && (!Math::is_finite(light.position) || !std::isfinite(light.range)
                        || light.range <= 0 || light.range > 1000000))
                || (!point
                    && (!Math::is_finite(light.direction)
                        || Math::dot(light.direction, light.direction) < 1e-12f
                        || !std::isfinite(Math::dot(light.direction, light.direction))))
                || (spot
                    && (!std::isfinite(light.inner_angle)
                        || !std::isfinite(light.outer_angle) || light.inner_angle < 0
                        || light.inner_angle >= light.outer_angle
                        || light.outer_angle >= 90))) {
                ++result.counts.z;
                continue;
            }
            sorted.push_back(&light);
        }
        std::stable_sort(sorted.begin(), sorted.end(),
            [](const auto* a, const auto* b) { return a->entity_id < b->entity_id; });
        result.counts.x = static_cast<float>(std::min(sorted.size(), size_t(MAX_LIGHTS)));
        result.counts.y = static_cast<float>(sorted.size()) - result.counts.x;
        for(size_t index = 0; index < static_cast<size_t>(result.counts.x); ++index) {
            const auto& light = *sorted[index];
            auto& packed = result.lights[index];
            Math::Vec3 position{};
            float range = 0;
            if(light.type != LightType::Directional) {
                position = light.position;
                range = light.range;
            }
            packed.position_type = Math::Vec4(position, static_cast<float>(light.type));
            Math::Vec3 direction{0, 0, -1};
            if(light.type != LightType::Point)
                direction = Math::normalize(light.direction);
            packed.direction_range = Math::Vec4(direction, range);
            packed.color_intensity = Math::Vec4(light.color, light.intensity);
            if(light.type == LightType::Spot)
                packed.cone = {std::cos(Math::radians(light.inner_angle)),
                    std::cos(Math::radians(light.outer_angle)), 0, 0};
        }
        return result;
    }
}
