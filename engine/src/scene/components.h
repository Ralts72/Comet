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
        // 世界位置与层级旋转，不含本地或祖先缩放；供相机、灯光与关节方向共用。
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

    struct COMET_API AnimatorComponent {
        bool enabled = true;
        std::string clip = "idle";
        float speed = 1;
        bool loop = true;
        float transition = 0.15f;
        // 非空时按所引用角色的实际移动状态选择动作。
        EntityUuid character;
        std::string idle = "idle", walk = "walk", run = "run";
        std::string crouch = "crouch", crouch_walk = "crouch_walk";
        std::string jump = "jump", fall = "fall";
        bool operator==(const AnimatorComponent&) const = default;
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
        std::is_same_v<std::remove_cvref_t<T>, AnimatorComponent>
        || std::is_same_v<std::remove_cvref_t<T>, MeshRendererComponent>
        || std::is_same_v<std::remove_cvref_t<T>, CameraComponent>
        || std::is_same_v<std::remove_cvref_t<T>, LightComponent>;

    struct COMET_API CameraControllerComponent {
        enum class Mode { Free, Orbit };
        bool enabled = true;
        Mode mode = Mode::Free;
        float move_speed = 3.0f;
        // 每个窗口逻辑像素对应的转角，单位为度。
        float look_sensitivity = 0.2f;
        // 转向轴满量程时的角速度，单位为度／秒。
        float look_speed = 120.0f;
        // Orbit 围绕父实体；距离与中心高度为世界米数，碰撞只缩短实际距离。
        float orbit_distance = 6.0f;
        float orbit_height = 1.2f;
        float orbit_min_distance = 1.5f;
        float orbit_max_distance = 12.0f;
        float orbit_zoom_step = 0.5f;
        float collision_radius = 0.15f;
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

    enum class ColliderShape { Box, Sphere, Capsule, ConvexHull, TriangleMesh };

    struct COMET_API ColliderComponent {
        ColliderShape shape = ColliderShape::Box;
        Math::Vec3 half_extents{0.5f};
        float radius = 0.5f;
        bool is_trigger = false;
        // 接触响应系数，范围 [0, 1]；触发器只保存配置，不参与碰撞响应。
        float friction = 0.2f;
        float restitution = 0.0f;
        // 胶囊沿本地 Y 轴，原点为中心；总高度包含两端半球。
        float height = 2.0f;
        uint32_t collision_layer = 1;
        uint32_t collision_mask = UINT32_MAX;
        // 网格碰撞使用绑定姿态的几何；不跟随蒙皮动画变形。
        AssetHandle mesh;
        [[nodiscard]] bool uses_mesh() const {
            return shape == ColliderShape::ConvexHull || shape == ColliderShape::TriangleMesh;
        }
        bool operator==(const ColliderComponent&) const = default;
    };

    struct COMET_API CharacterControllerComponent {
        bool enabled = true;
        // 竖直胶囊，Transform 原点为脚底；尺寸为本地米数，只支持正的等比缩放。
        float radius = 0.3f;
        float height = 1.8f;
        float move_speed = 4.0f;
        float run_speed = 6.0f;
        float crouch_speed = 2.0f;
        // 蹲伏高度不小于胶囊直径；形状变化保留脚底位置。
        float crouch_height_ratio = 0.6f;
        float air_control = 1.0f;
        float push_force = 100.0f;
        float jump_speed = 5.0f;
        float max_slope_angle = 45.0f;
        float step_height = 0.3f;
        float ground_snap_distance = 0.3f;
        float gravity_scale = 1.0f;
        uint32_t collision_layer = 1;
        uint32_t collision_mask = UINT32_MAX;
        bool operator==(const CharacterControllerComponent&) const = default;
    };

    struct COMET_API PointJointComponent {
        bool enabled = true;
        // 空引用连接到固定世界点；非空引用丢失时停用，不改成世界连接。
        EntityUuid connected_body;
        // 本地锚点随实体缩放；世界连接的 connected_anchor 是世界坐标。
        Math::Vec3 anchor{0};
        Math::Vec3 connected_anchor{0};
        float break_force = 0; // N，0 为不断裂；固定步超出阈值后取消启用。
        bool operator==(const PointJointComponent&) const = default;
    };

    enum class JointMotorMode { Velocity, Position };

    struct COMET_API HingeJointComponent {
        bool enabled = true;
        EntityUuid connected_body;
        // 锚点与连接语义同点关节；转轴只受旋转影响，不随缩放。
        Math::Vec3 anchor{0};
        Math::Vec3 connected_anchor{0};
        Math::Vec3 axis{0, 0, 1};
        // 连接刚体时为对方的本地方向，否则为世界方向；须非零且有限。
        Math::Vec3 connected_axis{0, 0, 1};
        bool limits_enabled = false;
        // 两端参考方向在世界中重合时为零度；投影到各自转轴的垂直平面。
        Math::Vec3 reference_axis{1, 0, 0};
        Math::Vec3 connected_reference_axis{1, 0, 0};
        // 本端相对连接端绕转轴的有符号角度，范围须包含零且宽度非零。
        float min_angle = -45;
        float max_angle = 45;
        float limits_frequency = 0; // Hz，0 为硬限位，正值为弹性限位。
        float limits_damping = 1;   // 阻尼比，1 为临界阻尼。
        bool motor_enabled = false;
        JointMotorMode motor_mode = JointMotorMode::Velocity;
        float motor_target = 0;      // 度，位置驱动目标；超出限位时夹到范围内。
        float motor_speed = 90;      // 度/秒，正值遵循本端转轴的右手方向。
        float motor_max_torque = 10; // N·m，0 表示不施加驱动力矩。
        float motor_frequency = 2;   // Hz，位置驱动的弹簧频率，最小 0.001。
        float motor_damping = 1;
        float break_force = 0;  // N，0 为不断裂。
        float break_torque = 0; // N·m，0 为不断裂；任一阈值超出后取消启用。
        [[nodiscard]] bool has_valid_angle_range() const {
            return std::isfinite(min_angle) && std::isfinite(max_angle) && min_angle >= -180
                   && min_angle <= 0 && max_angle >= 0 && max_angle <= 180 && min_angle < max_angle;
        }
        bool operator==(const HingeJointComponent&) const = default;
    };

    struct COMET_API SliderJointComponent {
        bool enabled = true;
        EntityUuid connected_body;
        // 锚点同点关节；重合时为零行程，只允许沿滑动轴平移。
        Math::Vec3 anchor{0};
        Math::Vec3 connected_anchor{0};
        Math::Vec3 axis{0, 1, 0};
        Math::Vec3 connected_axis{0, 1, 0};
        // 投影到滑动轴垂直平面，定义锁定朝向；方向只受旋转影响。
        Math::Vec3 reference_axis{1, 0, 0};
        Math::Vec3 connected_reference_axis{1, 0, 0};
        bool limits_enabled = false;
        float min_distance = -0.5f; // 米，本端锚点相对连接锚点沿滑动轴的位移。
        float max_distance = 0.5f;
        float limits_frequency = 0; // Hz，0 为硬限位，正值为弹性限位。
        float limits_damping = 1;
        bool motor_enabled = false;
        JointMotorMode motor_mode = JointMotorMode::Velocity;
        float motor_target = 0;     // 米，位置驱动目标；超出限位时夹到范围内。
        float motor_speed = 0.5f;   // m/s，本端相对连接端，正值沿本地滑动轴。
        float motor_max_force = 20; // N，0 表示不施加驱动力。
        float motor_frequency = 2;  // Hz，位置驱动的弹簧频率，最小 0.001。
        float motor_damping = 1;
        float break_force = 0;
        float break_torque = 0;
        [[nodiscard]] bool has_valid_distance_range() const {
            return std::isfinite(min_distance) && std::isfinite(max_distance) && min_distance <= 0
                   && max_distance >= 0 && min_distance < max_distance;
        }
        bool operator==(const SliderJointComponent&) const = default;
    };

    template<typename T>
    inline constexpr bool is_scene_joint_component_v =
        std::is_same_v<std::remove_cvref_t<T>, PointJointComponent>
        || std::is_same_v<std::remove_cvref_t<T>, HingeJointComponent>
        || std::is_same_v<std::remove_cvref_t<T>, SliderJointComponent>;

    template<typename T>
    inline constexpr bool is_scene_physics_component_v =
        std::is_same_v<std::remove_cvref_t<T>, RigidBodyComponent>
        || std::is_same_v<std::remove_cvref_t<T>, CharacterControllerComponent>
        || std::is_same_v<std::remove_cvref_t<T>, ColliderComponent>
        || is_scene_joint_component_v<T>;

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
