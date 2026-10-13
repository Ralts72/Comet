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
    // 时间和过渡归场景实例；片段、骨架和权重归不可变资产。
    struct AnimationPlayback {
        std::string clip;
        double time = 0;
        bool loop = true;
        std::string previous_clip;
        double previous_time = 0;
        bool previous_loop = true;
        float blend = 1;

        bool operator==(const AnimationPlayback&) const = default;
    };
    struct AnimationPose {
        std::vector<Math::Mat4> matrices;
        BoundingBox bounds;
    };
    [[nodiscard]] COMET_API Result<void> validate_animation(const AnimationData& data);
    [[nodiscard]] COMET_API Result<AnimationPose> sample_animation(
        const AnimationData& data, const AnimationPlayback* playback);
}
