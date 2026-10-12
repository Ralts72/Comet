#pragma once

#include "asset/handle.h"
#include "audio/audio_category.h"
#include "common/export.h"
#include "core/math_utils.h"
#include "scene/entity_id.h"
#include "scene/entity_uuid.h"
#include "scene/scene_settings.h"

#include <string>
#include <type_traits>

namespace Comet {
    struct COMET_API IdComponent {
        EntityId id = INVALID_ENTITY_ID;
    };

    struct COMET_API UuidComponent {
        EntityUuid uuid;
    };

    struct COMET_API NameComponent {
        std::string name = "Entity";
    };

    struct COMET_API TransformComponent {
        Math::Vec3 translation = Math::Vec3(0.0f);
        // 欧拉角，单位为度。
        Math::Vec3 rotation = Math::Vec3(0.0f);
        Math::Vec3 scale = Math::Vec3(1.0f);

        void rotate(const Math::Vec3& delta) { rotation = Math::wrap_degrees(rotation + delta); }

        [[nodiscard]] Math::Mat4 to_matrix() const {
            return Math::compose_trs(translation, rotation, scale);
        }
    };

    struct COMET_API RelationshipComponent {
        EntityId parent = INVALID_ENTITY_ID;
    };

    struct COMET_API WorldTransformComponent {
        uint64_t revision = 0;
        Math::Mat4 world_matrix = Math::Mat4(1.0f);
        // 世界位置与层级旋转，不含本地或祖先缩放；供相机和灯光共用。
        Math::Mat4 pose_world_matrix = Math::Mat4(1.0f);
    };

    template<typename T>
    inline constexpr bool is_scene_managed_component_v =
        std::is_same_v<std::remove_cvref_t<T>, IdComponent>
        || std::is_same_v<std::remove_cvref_t<T>, UuidComponent>
        || std::is_same_v<std::remove_cvref_t<T>, NameComponent>
        || std::is_same_v<std::remove_cvref_t<T>, RelationshipComponent>
        || std::is_same_v<std::remove_cvref_t<T>, WorldTransformComponent>;

    struct COMET_API MeshRendererComponent {
        AssetHandle mesh;
        AssetHandle material;
        bool operator==(const MeshRendererComponent&) const = default;
    };

    struct COMET_API LightComponent {
        LightType type = LightType::Directional;
        bool enabled = true;
        Math::Vec3 color{1.0f};
        float intensity = 1.0f;
        float range = 10.0f;
        // 本地 -Z 为出光方向；聚光半锥角，单位为度。
        float inner_angle = 20.0f;
        float outer_angle = 30.0f;
        bool casts_shadow = false;
        bool operator==(const LightComponent&) const = default;
    };

    struct COMET_API CameraComponent {
        enum class Projection { Perspective, Orthographic };

        bool primary = false;
        Projection projection = Projection::Perspective;
        // 垂直视场角，单位为度。
        float fov = 45.0f;
        float orthographic_height = 10.0f;
        float near_clip = 0.1f;
        float far_clip = 1000.0f;
        bool operator==(const CameraComponent&) const = default;
    };

    template<typename T>
    inline constexpr bool is_scene_render_component_v =
        std::is_same_v<std::remove_cvref_t<T>, MeshRendererComponent>
        || std::is_same_v<std::remove_cvref_t<T>, CameraComponent>
        || std::is_same_v<std::remove_cvref_t<T>, LightComponent>;

    struct COMET_API CameraControllerComponent {
        bool enabled = true;
        float move_speed = 3.0f;
        // 每个窗口逻辑像素对应的转角，单位为度。
        float look_sensitivity = 0.2f;
        // 转向轴满量程时的角速度，单位为度／秒。
        float look_speed = 120.0f;
    };

    struct COMET_API AudioSourceComponent {
        AudioSourceComponent();
        AudioSourceComponent(const AudioSourceComponent& other);
        AudioSourceComponent& operator=(const AudioSourceComponent& other);
        AudioSourceComponent(AudioSourceComponent&&) noexcept = default;
        AudioSourceComponent& operator=(AudioSourceComponent&&) noexcept = default;

        AssetHandle clip;
        bool play_on_start = true;
        bool loop = false;
        float volume = 0.5f;
        AudioCategory category = AudioCategory::Effects;
        [[nodiscard]] uint64_t lifetime() const noexcept { return m_lifetime; }

    private:
        uint64_t m_lifetime;
    };

    enum class BodyMotion { Static, Dynamic, Kinematic };
    enum class BodyMassMode { Mass, Density };

    struct COMET_API RigidBodyComponent {
        static constexpr float MIN_MASS = 0.001f;
        static constexpr float MIN_DENSITY = 0.001f;
        BodyMotion motion = BodyMotion::Dynamic;
        BodyMassMode mass_mode = BodyMassMode::Mass;
        // kg；质量模式保持指定值，密度模式按缩放后的碰撞体积计算。
        float mass = 1.0f;
        // kg/m³；仅动态刚体参与质量与惯性响应。
        float density = 1000.0f;
        // 仅动态非触发器使用线性扫掠；高速旋转和触发区仍不保证连续检测。
        bool continuous_collision = false;
        bool operator==(const RigidBodyComponent&) const = default;
    };

    enum class ColliderShape { Box, Sphere };

    struct COMET_API ColliderComponent {
        ColliderShape shape = ColliderShape::Box;
        Math::Vec3 half_extents{0.5f};
        float radius = 0.5f;
        bool is_trigger = false;
        // 接触响应系数，范围 [0, 1]；触发器只保存配置，不参与碰撞响应。
        float friction = 0.2f;
        float restitution = 0.0f;
        bool operator==(const ColliderComponent&) const = default;
    };

    struct COMET_API PointJointComponent {
        bool enabled = true;
        // 空引用连接到固定世界点；非空引用丢失时停用，不改成世界连接。
        EntityUuid connected_body;
        // 本地锚点随实体缩放；世界连接的 connected_anchor 是世界坐标。
        Math::Vec3 anchor{0};
        Math::Vec3 connected_anchor{0};
        bool operator==(const PointJointComponent&) const = default;
    };

    template<typename T>
    inline constexpr bool is_scene_physics_component_v =
        std::is_same_v<std::remove_cvref_t<T>, RigidBodyComponent>
        || std::is_same_v<std::remove_cvref_t<T>, ColliderComponent>
        || std::is_same_v<std::remove_cvref_t<T>, PointJointComponent>;

    template<typename T>
    inline constexpr bool is_scene_tracked_component_v =
        is_scene_render_component_v<T> || is_scene_physics_component_v<T>;

    template<typename T>
    inline constexpr bool is_scene_read_only_component_v =
        is_scene_tracked_component_v<T>
        || std::is_same_v<std::remove_cvref_t<T>, TransformComponent>
        || std::is_same_v<std::remove_cvref_t<T>, IdComponent>
        || std::is_same_v<std::remove_cvref_t<T>, UuidComponent>
        || std::is_same_v<std::remove_cvref_t<T>, RelationshipComponent>
        || std::is_same_v<std::remove_cvref_t<T>, WorldTransformComponent>;
}
