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
        Result<AnimationPose> solve_body(const AnimationData& data, AnimationPose pose,
            std::span<const AnimationIKConstraint> constraints, const AnimationBodyIK& settings,
            const AnimationRootMotion* root_motion) {
            PROFILE_SCOPE("Animation::body_ik");
            using Solved = Result<AnimationPose>;
            if(settings.root >= data.nodes.size() || settings.root == data.mesh_node
                || !std::isfinite(settings.max_translation) || settings.max_translation < 0
                || settings.max_translation > 2 || !std::isfinite(settings.max_rotation)
                || settings.max_rotation < 0 || settings.max_rotation > 60
                || !std::isfinite(settings.weight) || settings.weight < 0 || settings.weight > 1
                || !settings.iterations || settings.iterations > 12)
                return Solved::failure("Invalid full body IK settings");
            if(settings.weight == 0 || constraints.empty())
                return Solved::success(std::move(pose));
            const auto base = pose.local_transforms;
            std::vector<bool> body_nodes(data.nodes.size());
            const auto descendant = [&](uint32_t node, uint32_t ancestor) {
                while(node != NO_ANIMATION_NODE && node != ancestor)
                    node = data.nodes[node].parent;
                return node == ancestor;
            };
            for(const auto& target : constraints) {
                if(target.tip >= data.nodes.size() || !Math::is_finite(target.target)
                    || !std::isfinite(target.weight) || target.weight < 0 || target.weight > 1
                    || !descendant(target.tip, settings.root))
                    return Solved::failure("Full body IK target must be below the body root");
                auto node = target.tip;
                // 双骨段肢体由解析求解器处理；此处只优化它们共同依赖的躯干。
                for(int i = 0; i < 3 && node != NO_ANIMATION_NODE; ++i)
                    node = data.nodes[node].parent;
                while(node != NO_ANIMATION_NODE && descendant(node, settings.root)) {
                    body_nodes[node] = true;
                    if(node == settings.root)
                        break;
                    node = data.nodes[node].parent;
                }
            }
            Math::Vec3 translation(0);
            for(uint32_t iteration = 0; iteration < settings.iterations; ++iteration) {
                Math::Vec3 correction(0);
                float total = 0;
                for(const auto& target : constraints) {
                    const auto middle = data.nodes[target.tip].parent;
                    const auto upper =
                        middle < data.nodes.size() ? data.nodes[middle].parent : NO_ANIMATION_NODE;
                    if(upper >= data.nodes.size())
                        return Solved::failure("Full body IK requires two-bone limbs");
                    const auto a = bone_frame(data, pose, upper),
                               b = bone_frame(data, pose, middle),
                               c = bone_frame(data, pose, target.tip);
                    if(!a || !b || !c)
                        return Solved::failure("Full body IK limb is not a valid skin chain");
                    const float reach = glm::length(b.value().position - a.value().position)
                                        + glm::length(c.value().position - b.value().position);
                    const auto delta = target.target - a.value().position;
                    const float distance = glm::length(delta);
                    if(distance > reach && distance > 1e-6f)
                        correction += delta * ((distance - reach) / distance) * target.weight;
                    total += target.weight;
                }
                const auto next =
                    translation + correction * (settings.weight / std::max(total, 1.f));
                const float length = glm::length(next);
                const auto bounded = length > settings.max_translation && length > 0
                                         ? next * (settings.max_translation / length)
                                         : next;
                const auto root = bone_frame(data, pose, settings.root);
                if(!root)
                    return Solved::failure(root.error());
                const auto& local = pose.local_transforms[settings.root];
                const auto parent_rotation = root.value().rotation * glm::inverse(local.rotation);
                const auto root_scale = Math::uniform_scale(pose.bone_matrices[size_t(
                    std::ranges::find(data.joints, settings.root, &SkinJoint::node)
                    - data.joints.begin())]);
                auto moved = pose.local_transforms;
                moved[settings.root].translation += glm::inverse(parent_rotation)
                                                    * (bounded - translation)
                                                    / (*root_scale / local.scale.x);
                translation = bounded;
                auto shifted = build_animation_pose(
                    data, std::move(moved), root_motion, true, &pose.motion_correction);
                if(!shifted)
                    return shifted;
                pose = std::move(shifted).value();
                for(auto entry = data.evaluation_order.rbegin();
                    entry != data.evaluation_order.rend(); ++entry) {
                    const auto node = *entry;
                    if(!body_nodes[node])
                        continue;
                    const auto frame = bone_frame(data, pose, node);
                    if(!frame)
                        return Solved::failure(frame.error());
                    Math::Vec3 torque(0);
                    float denominator = 0;
                    for(const auto& target : constraints) {
                        if(!descendant(target.tip, node))
                            continue;
                        const auto tip = bone_frame(data, pose, target.tip);
                        if(!tip)
                            return Solved::failure(tip.error());
                        const auto arm = tip.value().position - frame.value().position;
                        torque +=
                            glm::cross(arm, target.target - frame.value().position) * target.weight;
                        denominator += glm::dot(arm, arm) * target.weight;
                    }
                    const float magnitude = glm::length(torque);
                    if(magnitude < 1e-6f || denominator < 1e-6f)
                        continue;
                    const auto turn =
                        Math::angle_axis(std::min(magnitude / denominator, .15f) * settings.weight,
                            torque / magnitude);
                    auto updated = pose.local_transforms;
                    const auto parent =
                        frame.value().rotation * glm::inverse(updated[node].rotation);
                    auto rotation =
                        glm::normalize(glm::inverse(parent) * turn * frame.value().rotation);
                    if(glm::dot(rotation, base[node].rotation) < 0)
                        rotation = -rotation;
                    const float angle =
                        2
                        * std::acos(std::clamp(glm::dot(rotation, base[node].rotation), 0.f, 1.f));
                    if(angle > glm::radians(settings.max_rotation))
                        rotation = glm::slerp(base[node].rotation, rotation,
                            glm::radians(settings.max_rotation) / angle);
                    updated[node].rotation = rotation;
                    auto rotated = build_animation_pose(
                        data, std::move(updated), root_motion, true, &pose.motion_correction);
                    if(!rotated)
                        return rotated;
                    pose = std::move(rotated).value();
                }
            }
            return Solved::success(std::move(pose));
        }
    }

    Result<AnimationPose> apply_animation_constraints(const AnimationData& data, AnimationPose pose,
        std::span<const AnimationIKConstraint> constraints, const AnimationRootMotion* root_motion,
        std::span<const AnimationPoseOffset> offsets, const AnimationBodyIK* body) {
        PROFILE_SCOPE("Animation::apply_ik");
        using Outcome = Result<AnimationPose>;
        if(pose.local_transforms.size() != data.nodes.size() || constraints.size() > 16
            || offsets.size() > 16)
            return Outcome::failure("Invalid IK pose or constraint count");
        if(!offsets.empty()) {
            auto local = pose.local_transforms;
            for(const auto& offset : offsets) {
                if(offset.node >= local.size() || !Math::is_finite(offset.translation))
                    return Outcome::failure("Invalid animation pose offset");
                local[offset.node].translation += offset.translation;
            }
            auto shifted = build_animation_pose(
                data, std::move(local), root_motion, true, &pose.motion_correction);
            if(!shifted)
                return shifted;
            pose = std::move(shifted).value();
        }
        if(body) {
            auto solved = solve_body(data, std::move(pose), constraints, *body, root_motion);
            if(!solved)
                return solved;
            pose = std::move(solved).value();
        }
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
            auto solved = build_animation_pose(
                data, std::move(local), root_motion, true, &pose.motion_correction);
            if(!solved)
                return solved;
            pose = std::move(solved).value();
        }
        return Outcome::success(std::move(pose));
    }
}
