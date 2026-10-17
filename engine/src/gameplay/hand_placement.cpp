#include "gameplay/hand_placement.h"
#include "animation/animation_data.h"
#include "scene/scene.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>
#include <algorithm>

namespace Comet::Gameplay {
    namespace {
        Result<void, Error> place_hands(
            Scene& scene, Entity model, const HandIKComponent& settings) {
            using Placed = Result<void, Error>;
            if(!std::isfinite(settings.weight) || settings.weight < 0 || settings.weight > 1
                || !Math::is_finite(settings.left_pole) || !Math::is_finite(settings.right_pole))
                return Placed::failure({"Invalid hand IK settings"});
            const auto frame = scene.get_animation_playback(model);
            if(!frame || !frame->pose || !frame->pose_source)
                return Placed::failure({"Hand IK requires an Animator and skin"});
            const auto world = scene.get_world_matrix(model);
            if(!Math::uniform_scale(world))
                return Placed::failure({"Hand IK requires positive uniform model scale"});
            const auto inverse = glm::affineInverse(world);
            const auto& data = *frame->pose_source;
            auto output = std::make_shared<AnimationPlayback>(*frame);
            const std::array targets{settings.left_target, settings.right_target};
            const std::array names{settings.left_hand, settings.right_hand};
            const std::array poles{settings.left_pole, settings.right_pole};
            for(size_t i = 0; i < targets.size(); ++i) {
                if(!targets[i])
                    continue;
                const Entity target = scene.find_entity(targets[i]);
                if(!target || target == model)
                    return Placed::failure({"Hand IK target is missing or refers to the model"});
                const auto joint = data.find_joint(names[i]);
                if(!joint)
                    return Placed::failure({joint.error()});
                const auto node = data.joints[joint.value()].node;
                if(std::ranges::any_of(
                       output->ik, [node](const auto& ik) { return ik.tip == node; }))
                    return Placed::failure({"Hand IK targets overlap an existing limb constraint"});
                const auto local = inverse * scene.get_world_matrix(target);
                AnimationIKConstraint constraint{.tip = node,
                    .target = Math::Vec3(local[3]),
                    .pole = poles[i],
                    .weight = settings.weight};
                if(settings.match_rotation) {
                    const auto scale = Math::uniform_scale(local);
                    if(!scale)
                        return Placed::failure(
                            {"Hand target rotation requires positive uniform scale"});
                    constraint.rotation =
                        glm::normalize(glm::quat_cast(Math::Mat3(local) / *scale));
                }
                output->ik.push_back(constraint);
            }
            scene.set_animation_playback(model, std::move(output));
            if(const auto applied = scene.get_animation_playback(model);
                applied && !applied->pose_error.empty())
                return Placed::failure({applied->pose_error});
            return Placed::success();
        }
    }
    Result<void, Error> HandPlacementSystem::place(Scene& scene) {
        auto result = Result<void, Error>::success();
        scene.each<const HandIKComponent, const AnimatorComponent>(
            [&](Entity model, const auto& settings, const auto& animator) {
                if(result && settings.enabled && animator.enabled
                    && (settings.left_target || settings.right_target))
                    result = place_hands(scene, model, settings);
            });
        return result;
    }
    Result<void, Error> HandPlacementSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices&) {
        return place(scene);
    }
    Result<void, Error> HandPlacementSystem::post_fixed_update(Scene& scene, const Context&) {
        return place(scene);
    }
}
