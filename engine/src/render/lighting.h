#pragma once

#include "scene/components.h"
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
    };

    // 与 lighting.glsl 的 std140 布局匹配；不是 Scene 组件或 GPU owner。
    struct alignas(16) COMET_API LightingData {
        static constexpr uint32_t MAX_LIGHTS = 32;
        struct Light {
            Math::Vec4 position_type{};
            Math::Vec4 direction_range{};
            Math::Vec4 color_intensity{};
            Math::Vec4 cone{};
        };
        std::array<Light, MAX_LIGHTS> lights{};
        Math::Vec4 counts{}; // 有效数量、超限数量、无效数量、保留。
        [[nodiscard]] static LightingData prepare(std::span<const RenderLight> lights);
    };
}
