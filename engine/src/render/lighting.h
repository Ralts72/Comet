#pragma once

#include "scene/components.h"
#include "core/geometry.h"
#include <array>
#include <span>

namespace Comet {
    struct RenderLight {
        EntityId entity_id = INVALID_ENTITY_ID;
        LightType type = LightType::Directional;
        Math::Vec3 position{};
        Math::Vec3 direction{0, 0, -1};
        Math::Vec3 color{1};
        float intensity = 1;
        float range = 10;
        float inner_angle = 20;
        float outer_angle = 30;
        bool casts_shadow = false;
    };

    // 与 forward.glsl 的 std140 布局匹配；不是 Scene 组件或 GPU owner。
    struct alignas(16) COMET_API LightingData {
        static constexpr uint32_t MAX_LIGHTS = 32;
        struct alignas(16) Light {
            Math::Vec3 position{};
            float type = 0;
            Math::Vec3 direction{};
            float range = 0;
            Math::Vec3 color{};
            float intensity = 0;
            float inner_cone_cos = 0;
            float outer_cone_cos = 0;
            float casts_shadow = 0;
            float reserved = 0;
        };
        std::array<Light, MAX_LIGHTS> lights{};
        float light_count = 0;
        float excess_lights = 0;
        float invalid_lights = 0;
        float reserved = 0;
        Math::Mat4 shadow_view_projection{1};
        float shadow_light_index = -1;
        float shadow_depth_bias = 0;
        float shadow_texel_size = 0;
        float shadow_reserved = 0;
        Math::Vec4 environment{0, 0, 0, 1}; // 强度、最大 LOD、旋转角正弦、旋转角余弦。
        [[nodiscard]] static LightingData prepare(std::span<const RenderLight> lights);
        void prepare_shadow(const BoundingBox& world_bounds, uint32_t resolution);
    };
}
