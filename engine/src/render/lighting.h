#pragma once

#include "common/export.h"
#include "common/result.h"
#include "core/geometry.h"
#include "scene/entity_id.h"
#include "scene/scene_settings.h"

#include <array>
#include <cstdint>
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
        float shadow_distance = 100;
    };

    struct RenderReflectionProbe {
        EntityId entity_id = INVALID_ENTITY_ID;
        AssetHandle environment;
        Math::Vec3 position{};
        BoundingBox bounds;
        float blend_distance = 1;
        float intensity = 1;
        float rotation = 0;
        bool box_projection = true;
    };

    struct RenderIrradianceVolume {
        EntityId entity_id = INVALID_ENTITY_ID;
        AssetHandle irradiance;
        float intensity = 1;
        float normal_bias = 0.05f;
        bool realtime = false;
        Math::Vec3 center{};
        Math::Vec3 half_extents{5, 2.5f, 5};
        float spacing = 1;
        float max_distance = 100;
        uint32_t update_budget = 8;
        uint32_t update_rays = 64;
        float history_weight = .9f;
        float bounce_strength = 0;
        float blend_distance = 1;
        [[nodiscard]] COMET_API Result<Math::Vec3u> realtime_dimensions() const;
    };

    // 与 forward.glsl 的 std140 布局匹配；不是 Scene 组件或 GPU owner。
    struct alignas(16) COMET_API LightingData {
        static constexpr uint32_t MAX_LIGHTS = 32;
        static constexpr uint32_t MAX_REFLECTION_PROBES = 4;
        static constexpr uint32_t MAX_IRRADIANCE_VOLUMES = 4;
        struct alignas(16) ReflectionProbe {
            Math::Vec4 position_intensity{};
            Math::Vec4 minimum_blend{};
            Math::Vec4 maximum_lod{};
            Math::Vec4 rotation_projection{}; // sin、cos、盒投影开关。
        };
        struct alignas(16) IrradianceVolume {
            Math::Vec4 origin_intensity{};
            Math::Vec4 spacing_bias{1, 1, 1, .05f};
            Math::Vec4 dimensions_blend{};
        };
        static constexpr uint32_t SHADOW_CASCADES = 4;
        static constexpr uint32_t MAX_SPOT_SHADOWS = 4;
        static constexpr uint32_t MAX_POINT_SHADOWS = 2;
        static constexpr uint32_t POINT_SHADOW_FACES = 6;
        static constexpr uint32_t POINT_SHADOW_START = SHADOW_CASCADES + MAX_SPOT_SHADOWS;
        static constexpr uint32_t MAX_SHADOW_VIEWS =
            POINT_SHADOW_START + MAX_POINT_SHADOWS * POINT_SHADOW_FACES;
        // 以局部光的半分辨率纹块为单位；方向光占上方 4×4，局部光按行排列。
        static constexpr uint32_t SHADOW_ATLAS_COLUMNS = 4;
        static constexpr uint32_t SHADOW_ATLAS_ROWS =
            4 + (MAX_SPOT_SHADOWS + MAX_POINT_SHADOWS * POINT_SHADOW_FACES + 3) / 4;
        struct alignas(16) Shadow {
            Math::Mat4 view_projection{};
            Math::Vec4 atlas_rect{}; // UV 缩放 xy、偏移 zw。
            float depth_bias = 0;
            float texel_size = 0;
            float light_index = -1;
            float reserved = 0;
        };
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
            float shadow_distance = 100;
        };
        std::array<Light, MAX_LIGHTS> lights{};
        float light_count = 0;
        float excess_lights = 0;
        float invalid_lights = 0;
        float reserved = 0;
        std::array<Shadow, MAX_SHADOW_VIEWS> shadows{};
        Math::Vec4 shadow_splits{};
        Math::Vec4 shadow_camera_depth{};
        float shadow_cascade_count = 0;
        float spot_shadow_count = 0;
        float excess_spot_shadows = 0;
        float shadow_blend = 0.1f;
        float point_shadow_count = 0;
        float excess_point_shadows = 0;
        Math::Vec2 shadow_reserved{};
        Math::Mat4 inverse_view_projection{1};
        Math::Vec4 ao_parameters{};         // 世界半径、强度；半径为零表示关闭。
        Math::Vec4 environment{0, 0, 0, 1}; // 强度、最大 LOD、旋转角正弦、旋转角余弦。
        std::array<ReflectionProbe, MAX_REFLECTION_PROBES> reflection_probes{};
        Math::Vec4 reflection_metadata{}; // 有效探针数、超预算数。
        std::array<IrradianceVolume, MAX_IRRADIANCE_VOLUMES> irradiance_volumes{};
        Math::Vec4 irradiance_metadata{}; // 有效区域数、超预算数。
        Math::Vec4 fog_parameters{};      // near（零关闭）、逆对数深度范围、屏幕尺寸的倒数。
        [[nodiscard]] static LightingData prepare(std::span<const RenderLight> lights);
        void prepare_shadow(const BoundingBox& world_bounds, uint32_t resolution,
            const Math::Mat4& view, const Math::Mat4& projection);

    private:
        void prepare_directional_shadow(const BoundingBox& world_bounds, uint32_t resolution,
            const Math::Mat4& view, const Math::Mat4& projection);
        void prepare_point_shadows(
            const BoundingBox& world_bounds, uint32_t resolution, const Frustum& camera_frustum);
        void prepare_spot_shadows(
            const BoundingBox& world_bounds, uint32_t resolution, const Frustum& camera_frustum);
    };
}
