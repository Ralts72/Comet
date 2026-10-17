#include "gameplay/pose_constraints.h"
#include "animation/animation_data.h"
#include "scene/scene.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>
#include <algorithm>

namespace Comet::Gameplay {
    namespace {
        struct LimbTarget {
            EntityUuid entity;
            const std::string& bone;
            Math::Vec3 pole;
        };
        Result<void, Error> append_targets(Scene& scene, Entity model, AnimationPlayback& output,
            std::span<const LimbTarget> targets, float weight, bool match_rotation,
            bool replace = false) {
            using Added = Result<void, Error>;
            if(!std::isfinite(weight) || weight < 0 || weight > 1)
                return Added::failure({"Invalid limb IK weight"});
            const auto world = scene.get_world_matrix(model);
            if(!Math::uniform_scale(world))
                return Added::failure({"Limb IK requires positive uniform model scale"});
            const auto inverse = glm::affineInverse(world);
            for(const auto& request : targets) {
                if(!request.entity)
                    continue;
                const Entity target = scene.find_entity(request.entity);
                if(!target || target == model || !Math::is_finite(request.pole))
                    return Added::failure({"IK target is missing, invalid or refers to the model"});
                const auto joint = output.pose_source->find_joint(request.bone);
                if(!joint)
                    return Added::failure({joint.error()});
                const auto node = output.pose_source->joints[joint.value()].node;
                const auto found = std::ranges::find(output.ik, node, &AnimationIKConstraint::tip);
                if(found != output.ik.end() && !replace)
                    return Added::failure({"IK targets overlap an existing limb constraint"});
                const auto local = inverse * scene.get_world_matrix(target);
                AnimationIKConstraint constraint{.tip = node,
                    .target = Math::Vec3(local[3]),
                    .pole = request.pole,
                    .weight = weight};
                if(match_rotation) {
                    const auto scale = Math::uniform_scale(local);
                    if(!scale)
                        return Added::failure(
                            {"IK target rotation requires positive uniform scale"});
                    constraint.rotation =
                        glm::normalize(glm::quat_cast(Math::Mat3(local) / *scale));
                }
                if(found != output.ik.end())
                    *found = constraint;
                else
                    output.ik.push_back(constraint);
            }
            return Added::success();
        }
        Result<void, Error> publish_pose(
            Scene& scene, Entity model, std::shared_ptr<AnimationPlayback> output) {
            scene.set_animation_playback(model, std::move(output));
            if(const auto applied = scene.get_animation_playback(model);
                applied && !applied->pose_error.empty())
                return Result<void, Error>::failure({applied->pose_error});
            return Result<void, Error>::success();
        }
        Result<void, Error> constrain(Scene& scene, Entity model, const HandIKComponent* hand,
            const FullBodyIKComponent* body) {
            const auto frame = scene.get_animation_playback(model);
            if(!frame || !frame->pose || !frame->pose_source)
                return Result<void, Error>::failure(
                    {"Pose constraints require an Animator and skin"});
            auto output = std::make_shared<AnimationPlayback>(*frame);
            if(hand) {
                const std::array targets{
                    LimbTarget{hand->left_target, hand->left_hand, hand->left_pole},
                    LimbTarget{hand->right_target, hand->right_hand, hand->right_pole}};
                if(auto added = append_targets(
                       scene, model, *output, targets, hand->weight, hand->match_rotation);
                    !added)
                    return added;
            }
            if(body) {
                const auto root = frame->pose_source->find_joint(body->root);
                if(!root)
                    return Result<void, Error>::failure({root.error()});
                const std::array targets{
                    LimbTarget{body->left_target, body->left_foot, body->left_pole},
                    LimbTarget{body->right_target, body->right_foot, body->right_pole}};
                if(auto added =
                        append_targets(scene, model, *output, targets, body->weight, true, true);
                    !added)
                    return added;
                output->body_ik = AnimationBodyIK{frame->pose_source->joints[root.value()].node,
                    body->max_translation, body->max_rotation, body->weight, body->iterations};
            }
            return publish_pose(scene, model, std::move(output));
        }
    }
    Result<void, Error> PoseConstraintSystem::place(Scene& scene) {
        auto result = Result<void, Error>::success();
        scene.each<const AnimatorComponent>([&](Entity model, const auto& animator) {
            if(!result || !animator.enabled)
                return;
            const HandIKComponent* hand = nullptr;
            const FullBodyIKComponent* body = nullptr;
            if(model.has_component<HandIKComponent>()) {
                const auto& settings = model.get_component<HandIKComponent>();
                if(settings.enabled && (settings.left_target || settings.right_target))
                    hand = &settings;
            }
            if(model.has_component<FullBodyIKComponent>()) {
                const auto& settings = model.get_component<FullBodyIKComponent>();
                if(settings.enabled)
                    body = &settings;
            }
            if(hand || body)
                result = constrain(scene, model, hand, body);
        });
        return result;
    }
    Result<void, Error> PoseConstraintSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices&) {
        return place(scene);
    }
    Result<void, Error> PoseConstraintSystem::post_fixed_update(Scene& scene, const Context&) {
        return place(scene);
    }
}
