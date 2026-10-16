#include "animation/animation_data.h"
#include "diagnostics/profiler.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Comet {
    namespace {
        struct BoneFrame {
            Math::Vec3 position;
            Math::Quat rotation;
        };
        Result<BoneFrame> bone_frame(
            const AnimationData& data, const AnimationPose& pose, uint32_t node) {
            const auto found = std::ranges::find(data.joints, node, &SkinJoint::node);
            if(found == data.joints.end() || pose.bone_matrices.size() != data.joints.size())
                return Result<BoneFrame>::failure("IK chain must belong to the skin");
            const auto& matrix = pose.bone_matrices[std::size_t(found - data.joints.begin())];
            const auto scale = Math::uniform_scale(matrix);
            if(!scale)
                return Result<BoneFrame>::failure(
                    "IK requires positive uniform scales without shear");
            const auto basis = Math::Mat3(matrix) / *scale;
            return Result<BoneFrame>::success(
                {Math::Vec3(matrix[3]), glm::normalize(glm::quat_cast(basis))});
        }
    }

    Result<AnimationPose> apply_animation_ik(const AnimationData& data, AnimationPose pose,
        std::span<const AnimationIKConstraint> constraints,
        const AnimationRootMotion* root_motion) {
        PROFILE_SCOPE("Animation::apply_ik");
        using Outcome = Result<AnimationPose>;
        if(pose.local_transforms.size() != data.nodes.size() || constraints.size() > 16)
            return Outcome::failure("Invalid IK pose or constraint count");
        for(const auto& constraint : constraints) {
            if(!Math::is_finite(constraint.target) || !Math::is_finite(constraint.pole)
                || !std::isfinite(constraint.weight) || constraint.weight < 0
                || constraint.weight > 1)
                return Outcome::failure("Invalid IK target or weight");
            if(constraint.rotation
                && (!Math::is_finite(Math::Vec4(constraint.rotation->x, constraint.rotation->y,
                        constraint.rotation->z, constraint.rotation->w))
                    || glm::length(*constraint.rotation) < 1e-6f))
                return Outcome::failure("Invalid IK target rotation");
            if(constraint.tip >= data.nodes.size())
                return Outcome::failure("IK tip node is missing");
            const uint32_t tip = constraint.tip;
            const uint32_t middle = data.nodes[tip].parent;
            const uint32_t root =
                middle < data.nodes.size() ? data.nodes[middle].parent : NO_ANIMATION_NODE;
            if(root >= data.nodes.size())
                return Outcome::failure("IK requires a direct three-joint chain");
            if(constraint.weight == 0)
                continue;
            auto a = bone_frame(data, pose, root);
            auto b = bone_frame(data, pose, middle);
            auto c = bone_frame(data, pose, tip);
            if(!a)
                return Outcome::failure(a.error());
            if(!b)
                return Outcome::failure(b.error());
            if(!c)
                return Outcome::failure(c.error());
            const auto origin = a.value().position;
            const auto upper = b.value().position - origin;
            const auto lower = c.value().position - b.value().position;
            const float first_length = glm::length(upper), second_length = glm::length(lower);
            const float epsilon = std::max(first_length + second_length, 1.f) * 1e-5f;
            if(first_length <= epsilon || second_length <= epsilon)
                return Outcome::failure("IK bone lengths must be positive");
            auto direction = constraint.target - origin;
            float distance = glm::length(direction);
            if(distance <= epsilon) {
                direction = upper / first_length;
                distance = 0;
            } else
                direction /= distance;
            // 保持骨段长度；不可达目标在可达半径内钳制，不拉长腿或反折膝盖。
            distance = std::clamp(distance, std::abs(first_length - second_length) + epsilon,
                first_length + second_length - epsilon);
            const auto target = origin + direction * distance;
            auto bend = constraint.pole - origin;
            bend -= direction * glm::dot(bend, direction);
            if(glm::length(bend) <= epsilon) {
                bend = upper - direction * glm::dot(upper, direction);
                if(glm::length(bend) <= epsilon) {
                    const auto axis =
                        std::abs(direction.x) < .8f ? Math::Vec3(1, 0, 0) : Math::Vec3(0, 1, 0);
                    bend = axis - direction * glm::dot(axis, direction);
                }
            }
            bend = glm::normalize(bend);
            const float along =
                (first_length * first_length - second_length * second_length + distance * distance)
                / (2 * distance);
            const float height =
                std::sqrt(std::max(0.f, first_length * first_length - along * along));
            const auto joint = origin + direction * along + bend * height;
            const auto first_turn =
                glm::rotation(upper / first_length, glm::normalize(joint - origin));
            const auto after_middle = origin + first_turn * upper;
            const auto after_tip = origin + first_turn * (c.value().position - origin);
            const auto second_turn = glm::rotation(
                glm::normalize(after_tip - after_middle), glm::normalize(target - after_middle));
            auto local = pose.local_transforms;
            const auto first_parent = a.value().rotation * glm::inverse(local[root].rotation);
            const auto first_rotation =
                glm::inverse(first_parent) * first_turn * a.value().rotation;
            const auto second_rotation = glm::inverse(first_turn * a.value().rotation) * second_turn
                                         * first_turn * b.value().rotation;
            local[root].rotation = glm::normalize(glm::slerp(
                local[root].rotation, glm::normalize(first_rotation), constraint.weight));
            local[middle].rotation = glm::normalize(glm::slerp(
                local[middle].rotation, glm::normalize(second_rotation), constraint.weight));
            if(constraint.rotation) {
                const auto tip_rotation =
                    glm::inverse(second_turn * first_turn * b.value().rotation)
                    * glm::normalize(*constraint.rotation);
                local[tip].rotation = glm::normalize(glm::slerp(
                    local[tip].rotation, glm::normalize(tip_rotation), constraint.weight));
            }
            auto solved = build_animation_pose(data, std::move(local), root_motion);
            if(!solved)
                return solved;
            pose = std::move(solved).value();
        }
        return Outcome::success(std::move(pose));
    }
}
