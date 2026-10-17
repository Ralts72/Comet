#pragma once
#include "common/export.h"
#include "common/result.h"
#include "core/geometry.h"
#include "core/math_utils.h"
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <span>
#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    inline constexpr uint32_t NO_ANIMATION_NODE = std::numeric_limits<uint32_t>::max();
    struct JointTransform {
        Math::Vec3 translation{0};
        glm::quat rotation{1, 0, 0, 0};
        Math::Vec3 scale{1};
    };
    struct AnimationNode {
        std::string name;
        uint32_t parent = NO_ANIMATION_NODE;
        JointTransform rest;
    };
    struct SkinJoint {
        uint32_t node;
        Math::Mat4 inverse_bind{1};
        std::optional<BoundingBox> bounds;
    };
    struct AnimationChannel {
        enum class Path { Translation, Rotation, Scale };
        enum class Interpolation { Linear, Step, CubicSpline };
        uint32_t node;
        Path path;
        Interpolation interpolation;
        std::vector<float> times;
        std::vector<Math::Vec4> values;
    };
    struct AnimationClip {
        std::string name;
        float duration = 0;
        std::vector<AnimationChannel> channels;
    };
    struct AnimationData {
        uint32_t mesh_node = NO_ANIMATION_NODE;
        std::vector<AnimationNode> nodes;
        std::vector<uint32_t> evaluation_order;
        std::vector<SkinJoint> joints;
        std::vector<AnimationClip> clips;

        [[nodiscard]] COMET_API const AnimationClip* find_clip(std::string_view name) const;
    };
    struct AnimationBlendWeight {
        std::string clip;
        float weight = 0;
        bool operator==(const AnimationBlendWeight&) const = default;
    };
    // clip 使用剩余权重；blends 保存额外片段。所有片段共享时间／周期相位。
    struct AnimationBlend {
        std::string clip;
        double time = 0;
        bool loop = true;
        std::vector<AnimationBlendWeight> blends;
        bool normalized_time = false;
        [[nodiscard]] bool has_clip() const { return !clip.empty() || !blends.empty(); }
        bool operator==(const AnimationBlend&) const = default;
    };
    struct AnimationBlendPoint {
        float threshold;
        std::string clip;
        float y = 0;
        bool operator==(const AnimationBlendPoint&) const = default;
    };
    [[nodiscard]] COMET_API Result<AnimationBlend> blend_animation_1d(
        std::span<const AnimationBlendPoint> points, float parameter);
    [[nodiscard]] COMET_API Result<AnimationBlend> blend_animation_2d(
        std::span<const AnimationBlendPoint> points, Math::Vec2 parameter);
    struct AnimationLayer {
        enum class Mode { Override, Additive };
        AnimationBlend sample;
        float weight = 1;
        // 命名节点及其全部后代；空遮罩覆盖全身。
        std::vector<std::string> mask;
        Mode mode = Mode::Override;
        // 加法层以绑定姿态为默认参考；也可指定片段在某一秒的姿态。
        std::string reference_clip;
        float reference_time = 0;
        bool operator==(const AnimationLayer&) const = default;
    };
    [[nodiscard]] COMET_API Result<float> animation_duration(
        const AnimationData& data, const AnimationBlend& sample);
    [[nodiscard]] COMET_API double animation_sample_time(double time, double duration, bool loop);

    struct AnimationRootMotion {
        std::string node;
        bool horizontal = true;
        bool vertical = false;
        bool rotation = true;
        bool operator==(const AnimationRootMotion&) const = default;
    };
    struct AnimationMotion {
        Math::Vec3 translation{0};
        float yaw = 0;          // 弧度，绕源资产 Y 轴。
        Math::Vec3 up{0, 1, 0}; // 源资产 Y 轴在网格局部空间中的方向。
    };
    // to 与 sample.time 使用相同时间单位；跨圈保留位移和旋转，不从取模后的时间反推。
    [[nodiscard]] COMET_API Result<AnimationMotion> animation_root_motion(const AnimationData& data,
        const AnimationBlend& sample, double to, const AnimationRootMotion& settings);

    struct AnimationIKConstraint {
        uint32_t tip = NO_ANIMATION_NODE;
        // 网格局部空间的目标和膝／肘的弯曲提示点；直接父链组成两个骨段。
        Math::Vec3 target{0};
        Math::Vec3 pole{0};
        std::optional<Math::Quat> rotation;
        float weight = 1;
        bool operator==(const AnimationIKConstraint&) const = default;
    };

    struct AnimationPoseOffset {
        uint32_t node = NO_ANIMATION_NODE;
        // 节点父空间的局部位移，先于 IK 应用，不改变实体或碰撞体。
        Math::Vec3 translation{0};
        bool operator==(const AnimationPoseOffset&) const = default;
    };

    // 借用本次采样定义；from／to 保留取模前的区间，消费者可计算窗口剩余轨迹。
    struct AnimationMotionStep {
        const AnimationData& data;
        const AnimationBlend& sample;
        std::string_view state;
        double from;
        double to;
        AnimationMotion motion;
    };

    // 时间和过渡归场景实例；混合空间与状态过渡分别求值，不改变共享片段。
    struct AnimationPose;
    struct AnimationPlayback {
        AnimationBlend current;
        AnimationBlend previous;
        float blend = 1;
        std::string state;
        std::string previous_state;
        std::vector<AnimationLayer> layers;
        std::vector<AnimationIKConstraint> ik;
        std::vector<AnimationPoseOffset> offsets;
        std::optional<AnimationRootMotion> root_motion;
        // 动画库绑定后的只读片段，节点顺序与目标网格一致。
        std::shared_ptr<const AnimationData> clips;
        std::shared_ptr<const AnimationData> clips_target;
        // 挂点或固定步实例在 Main 采样，渲染线程借用同一份不可变姿态。
        std::shared_ptr<const AnimationData> pose_source;
        std::shared_ptr<const AnimationPose> pose;
        std::string pose_error;
        bool operator==(const AnimationPlayback& other) const {
            return current == other.current && previous == other.previous && blend == other.blend
                   && state == other.state && previous_state == other.previous_state
                   && layers == other.layers && ik == other.ik && offsets == other.offsets
                   && root_motion == other.root_motion && clips == other.clips
                   && clips_target == other.clips_target;
        }
    };
    struct AnimationPose {
        // 保留局部 TRS，供固定步呈现插值和姿态约束复用。
        std::vector<JointTransform> local_transforms;
        std::vector<Math::Mat4> matrices;
        // 与 joints 同序，网格局部空间，不含 inverse_bind，供骨骼挂点使用。
        std::vector<Math::Mat4> bone_matrices;
        BoundingBox bounds;
    };
    [[nodiscard]] COMET_API Result<void> validate_animation(const AnimationData& data);
    [[nodiscard]] COMET_API Result<AnimationPose> sample_animation(
        const AnimationData& data, const AnimationPlayback* playback, bool include_bones = true);
    [[nodiscard]] COMET_API Result<AnimationPose> build_animation_pose(const AnimationData& data,
        std::vector<JointTransform> local, const AnimationRootMotion* root_motion = nullptr,
        bool include_bones = true);
    [[nodiscard]] COMET_API Result<AnimationPose> apply_animation_constraints(
        const AnimationData& data, AnimationPose pose,
        std::span<const AnimationIKConstraint> constraints,
        const AnimationRootMotion* root_motion = nullptr,
        std::span<const AnimationPoseOffset> offsets = {});
    [[nodiscard]] COMET_API Result<AnimationPose> interpolate_animation_pose(
        const AnimationData& data, const AnimationPose& previous, const AnimationPose& current,
        float alpha, const AnimationRootMotion* root_motion = nullptr);
    // 模型替换后的旧过渡可缺失；返回可显示姿态，并保留原始诊断。
    [[nodiscard]] COMET_API Result<AnimationPose> resolve_animation_pose(const AnimationData& data,
        const AnimationPlayback* playback, std::string& error, bool include_bones = true);
}
