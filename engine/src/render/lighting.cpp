#include "render/lighting.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

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
    static_assert(sizeof(LightingData::Shadow) == 96);
    static_assert(offsetof(LightingData::Shadow, atlas_rect) == 64);
    static_assert(offsetof(LightingData::Shadow, depth_bias) == 80);
    static_assert(offsetof(LightingData::Shadow, light_index) == 88);
    static_assert(offsetof(LightingData, shadows) == LightingData::MAX_LIGHTS * 64 + 16);
    static_assert(offsetof(LightingData, shadow_splits) == LightingData::MAX_LIGHTS * 64 + 784);
    static_assert(
        offsetof(LightingData, shadow_camera_depth) == LightingData::MAX_LIGHTS * 64 + 800);
    static_assert(
        offsetof(LightingData, shadow_cascade_count) == LightingData::MAX_LIGHTS * 64 + 816);
    static_assert(sizeof(LightingData) == LightingData::MAX_LIGHTS * 64 + 848);

    static Math::Mat4 light_view_matrix(
        const Math::Vec3& direction, const Math::Vec3& position = {}) {
        const auto up = std::abs(direction.y) > 0.95f ? Math::Vec3(1, 0, 0) : Math::Vec3(0, 1, 0);
        return Math::look_at(Math::Vec3(0), direction, up)
               * Math::translate(Math::Mat4(1), -position);
    }

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
        if(light.type == LightType::Directional && light.casts_shadow
            && (!std::isfinite(light.shadow_distance) || light.shadow_distance <= 0
                || light.shadow_distance > 10000))
            return false;
        return true;
    }

    LightingData LightingData::prepare(const std::span<const RenderLight> input) {
        LightingData result;
        std::array<const RenderLight*, MAX_LIGHTS> sorted{};
        size_t count = 0;
        size_t valid_count = 0;
        const auto order = [](const RenderLight* a, const RenderLight* b) {
            return a->entity_id != b->entity_id ? a->entity_id < b->entity_id : a < b;
        };
        for(const auto& light : input) {
            if(!is_valid_light(light)) {
                ++result.invalid_lights;
                continue;
            }
            ++valid_count;
            if(count < MAX_LIGHTS) {
                sorted[count++] = &light;
                continue;
            }
            if(valid_count == MAX_LIGHTS + 1)
                std::make_heap(sorted.begin(), sorted.end(), order);
            if(order(&light, sorted.front())) {
                std::pop_heap(sorted.begin(), sorted.end(), order);
                sorted.back() = &light;
                std::push_heap(sorted.begin(), sorted.end(), order);
            }
        }
        std::sort(sorted.begin(), sorted.begin() + count, order);
        result.light_count = static_cast<float>(count);
        result.excess_lights = static_cast<float>(valid_count - count);
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
            packed.shadow_distance = light.shadow_distance;
        }
        return result;
    }

    void LightingData::prepare_shadow(const BoundingBox& world_bounds, const uint32_t resolution,
        const Math::Mat4& camera_view, const Math::Mat4& camera_projection) {
        shadows = {};
        shadow_splits = {};
        shadow_camera_depth = {};
        shadow_cascade_count = 0;
        spot_shadow_count = 0;
        excess_spot_shadows = 0;
        if(!world_bounds.is_valid() || resolution < 4 || !std::isfinite(light_count)
            || Math::length(world_bounds.size()) < 1e-5f)
            return;
        const auto camera_frustum = Frustum::from_view_projection(camera_projection * camera_view);
        if(!camera_frustum)
            return;
        prepare_directional_shadow(world_bounds, resolution, camera_view, camera_projection);
        prepare_spot_shadows(world_bounds, resolution / 2, *camera_frustum);
    }

    void LightingData::prepare_directional_shadow(const BoundingBox& world_bounds,
        const uint32_t resolution, const Math::Mat4& camera_view,
        const Math::Mat4& camera_projection) {
        const auto count = uint32_t(std::clamp(light_count, 0.0f, float(MAX_LIGHTS)));
        uint32_t selected = count;
        for(uint32_t index = 0; index < count; ++index) {
            const auto& light = lights[index];
            if(light.type == float(LightType::Directional) && light.casts_shadow != 0
                && light.intensity > 0) {
                selected = index;
                break;
            }
        }
        if(selected == count)
            return;
        const auto inverse_projection = Math::inverse(camera_projection);
        const auto inverse_view = Math::inverse(camera_view);
        std::array<Math::Vec3, 8> corners;
        std::array<Math::Vec3, 2> centers{};
        for(unsigned index = 0; index < corners.size(); ++index) {
            auto point = inverse_projection
                         * Math::Vec4(index & 1 ? 1 : -1, index & 2 ? 1 : -1, index & 4 ? 1 : 0, 1);
            if(!Math::is_finite(point) || std::abs(point.w) < 1e-8f)
                return;
            point /= point.w;
            centers[index / 4] += Math::Vec3(point) * 0.25f;
            const auto world = inverse_view * point;
            if(!Math::is_finite(world) || std::abs(world.w) < 1e-8f)
                return;
            corners[index] = Math::Vec3(world) / world.w;
        }
        const float view_length = Math::length(centers[1] - centers[0]);
        if(!std::isfinite(view_length) || view_length < 1e-5f)
            return;
        const auto forward = (centers[1] - centers[0]) / view_length;
        const float near_depth = Math::dot(centers[0], forward);
        const float camera_far = Math::dot(centers[1], forward);
        const float far_depth = std::min(camera_far, near_depth + lights[selected].shadow_distance);
        if(!std::isfinite(far_depth) || far_depth <= near_depth)
            return;
        shadow_camera_depth = Math::transpose(camera_view) * Math::Vec4(forward, 0);
        shadow_camera_depth.w -= near_depth;
        // 固定光源基底；只吸附投影中心，不让相机平移改变光源坐标原点。
        const auto light_view = light_view_matrix(lights[selected].direction);
        const auto caster_bounds = transform_box(world_bounds, light_view);
        if(!caster_bounds)
            return;
        float previous = near_depth;
        float previous_start = near_depth;
        for(unsigned cascade = 0; cascade < SHADOW_CASCADES; ++cascade) {
            const float fraction = float(cascade + 1) / SHADOW_CASCADES;
            const float linear = near_depth + (far_depth - near_depth) * fraction;
            float split = linear;
            if(near_depth > 0 && std::abs(camera_projection[3][3]) < 1e-6f) {
                const float logarithmic = near_depth * std::pow(far_depth / near_depth, fraction);
                split = 0.4f * linear + 0.6f * logarithmic;
            }
            shadow_splits[cascade] = split - near_depth;
            const float start = previous - (previous - previous_start) * shadow_blend;
            const float first = (start - near_depth) / view_length;
            const float last = (split - near_depth) / view_length;
            std::array<Math::Vec3, 8> slice;
            Math::Vec3 center{};
            for(unsigned corner = 0; corner < 4; ++corner) {
                const auto edge = corners[corner + 4] - corners[corner];
                slice[corner] = corners[corner] + edge * first;
                slice[corner + 4] = corners[corner] + edge * last;
                center += (slice[corner] + slice[corner + 4]) * 0.125f;
            }
            float radius = 0;
            for(const auto point : slice)
                radius = std::max(radius, Math::length(point - center));
            // 球形包围保证相机旋转时比例稳定；额外一圈 texel 容纳吸附与 PCF。
            radius = std::ceil(radius * 16) / 16;
            radius *= float(resolution) / float(resolution - 2);
            if(!std::isfinite(radius) || radius <= 0)
                return;
            const float texel = 2 * radius / resolution;
            const auto light_center = light_view * Math::Vec4(center, 1);
            const float x = std::round(light_center.x / texel) * texel;
            const float y = std::round(light_center.y / texel) * texel;
            auto depth = *caster_bounds;
            for(const auto point : slice)
                depth.include(Math::Vec3(light_view * Math::Vec4(point, 1)));
            const float padding = std::max(radius * 0.05f, 0.01f);
            const float near_plane = -depth.maximum.z - padding;
            const float far_plane = -depth.minimum.z + padding;
            auto& shadow = shadows[cascade];
            shadow.view_projection =
                Math::ortho(x - radius, x + radius, y - radius, y + radius, near_plane, far_plane)
                * light_view;
            shadow.depth_bias = 0.25f * texel / (far_plane - near_plane);
            shadow.texel_size = 1.0f / resolution;
            shadow.atlas_rect = {0.5f, 0.4f, float(cascade % 2) * 0.5f, float(cascade / 2) * 0.4f};
            previous_start = previous;
            previous = split;
        }
        for(unsigned cascade = 0; cascade < SHADOW_CASCADES; ++cascade)
            shadows[cascade].light_index = float(selected);
        shadow_cascade_count = SHADOW_CASCADES;
    }

    void LightingData::prepare_spot_shadows(
        const BoundingBox& world_bounds, const uint32_t resolution, const Frustum& camera_frustum) {
        const auto count = uint32_t(std::clamp(light_count, 0.0f, float(MAX_LIGHTS)));
        for(uint32_t index = 0; index < count; ++index) {
            const auto& light = lights[index];
            if(light.type != float(LightType::Spot) || light.casts_shadow == 0
                || light.intensity <= 0)
                continue;
            const float angle = std::acos(std::clamp(light.outer_cone_cos, -1.0f, 1.0f));
            const float tangent = std::tan(angle);
            if(!std::isfinite(tangent) || tangent <= 0)
                continue;
            const auto view = light_view_matrix(light.direction, light.position);
            const float radius = light.range * tangent;
            const auto bounds = transform_box(
                {{-radius, -radius, -light.range}, {radius, radius, 0}}, Math::inverse(view));
            if(!bounds || !camera_frustum.intersects(*bounds))
                continue;
            const float near_plane = std::min(0.05f, light.range * 0.01f);
            const auto matrix =
                Math::perspective(2 * angle * Math::RAD_TO_DEG, 1, near_plane, light.range) * view;
            const auto frustum = Frustum::from_view_projection(matrix);
            if(!frustum || !frustum->intersects(world_bounds))
                continue;
            if(spot_shadow_count >= MAX_SPOT_SHADOWS) {
                ++excess_spot_shadows;
                continue;
            }
            const auto slot = uint32_t(spot_shadow_count++);
            auto& shadow = shadows[SHADOW_CASCADES + slot];
            shadow.view_projection = matrix;
            shadow.atlas_rect = {0.25f, 0.2f, float(slot) * 0.25f, 0.8f};
            shadow.texel_size = 1.0f / resolution;
            shadow.light_index = float(index);
            // 透视投影下一个世界纹素的深度偏移；Shader 再除以接收点的 clip.w。
            shadow.depth_bias = 0.5f * tangent * near_plane * light.range
                                / (resolution * (light.range - near_plane));
        }
    }
}
