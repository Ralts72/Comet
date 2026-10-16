#include "gameplay/foot_placement.h"
#include "animation/animation_data.h"
#include "physics/physics_access.h"
#include "scene/scene.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Comet::Gameplay {
    namespace {
        bool valid(const FootPlacementComponent& settings) {
            return std::isfinite(settings.weight) && settings.weight >= 0 && settings.weight <= 1
                   && std::isfinite(settings.foot_height) && settings.foot_height >= 0
                   && std::isfinite(settings.max_offset) && settings.max_offset >= 0
                   && std::isfinite(settings.plant_height) && settings.plant_height > 0
                   && std::isfinite(settings.probe_up) && settings.probe_up >= 0
                   && std::isfinite(settings.probe_down) && settings.probe_down > 0
                   && std::isfinite(settings.max_slope) && settings.max_slope >= 0
                   && settings.max_slope < 90 && std::isfinite(settings.response)
                   && settings.response > 0 && Math::is_finite(settings.knee_forward)
                   && glm::length(settings.knee_forward) > 1e-5f
                   && (!settings.left_foot.empty() || !settings.right_foot.empty())
                   && (settings.left_foot.empty() || settings.left_foot != settings.right_foot);
        }
        Result<std::size_t> foot_joint(const AnimationData& data, const std::string& name) {
            const auto node = std::ranges::find(data.nodes, name, &AnimationNode::name);
            if(node == data.nodes.end()
                || std::ranges::count(data.nodes, name, &AnimationNode::name) != 1)
                return Result<std::size_t>::failure("Missing or ambiguous foot bone: " + name);
            const auto index = uint32_t(node - data.nodes.begin());
            const auto joint = std::ranges::find(data.joints, index, &SkinJoint::node);
            if(joint == data.joints.end())
                return Result<std::size_t>::failure("Foot bone is not part of the skin: " + name);
            return Result<std::size_t>::success(std::size_t(joint - data.joints.begin()));
        }
    }
    Result<void, Error> FootPlacementSystem::on_start(
        Scene&, RuntimeSession&, const RuntimeServices&) {
        m_states.clear();
        return Result<void, Error>::success();
    }
    Result<void, Error> FootPlacementSystem::place(Scene& scene, Entity model,
        const FootPlacementComponent& settings, const Context& context, State& state) {
        if(!valid(settings))
            return Result<void, Error>::failure({"Invalid foot placement settings"});
        const auto frame = scene.get_animation_playback(model);
        if(!frame || !frame->pose || !frame->pose_source || !context.services.physics)
            return Result<void, Error>::failure(
                {"Foot placement requires an Animator, skin and physics world"});
        if(state.source != frame->pose_source) {
            state = {};
            state.source = frame->pose_source;
        }
        auto actor =
            settings.character ? scene.find_entity(settings.character) : scene.get_parent(model);
        if(settings.character && !actor)
            return Result<void, Error>::failure({"Foot placement character is missing"});
        if(!actor)
            actor = model;
        auto ancestor = model;
        while(ancestor && ancestor != actor)
            ancestor = scene.get_parent(ancestor);
        if(!ancestor)
            return Result<void, Error>::failure(
                {"Foot placement character must be the model or an ancestor"});
        bool grounded = true;
        if(actor.has_component<CharacterControllerComponent>()) {
            const auto character = context.services.physics->get_character_state(actor);
            grounded = character && character->grounded
                       && actor.get_component<CharacterControllerComponent>().enabled;
        }
        const auto world = scene.get_world_matrix(model);
        const auto scale = Math::uniform_scale(world);
        if(!scale)
            return Result<void, Error>::failure(
                {"Foot placement requires positive uniform model scale"});
        const auto orientation = glm::normalize(glm::quat_cast(Math::Mat3(world) / *scale));
        const auto inverse = glm::affineInverse(world);
        const float alpha = float(-std::expm1(-context.delta_time * settings.response));
        const Math::Vec3 up{0, 1, 0};
        const std::array names{settings.left_foot, settings.right_foot};
        auto output = std::make_shared<AnimationPlayback>(*frame);
        output->ik.clear();
        for(std::size_t i = 0; i < names.size(); ++i) {
            if(names[i].empty())
                continue;
            const auto joint = foot_joint(*state.source, names[i]);
            if(!joint)
                return Result<void, Error>::failure({joint.error()});
            const auto node = state.source->joints[joint.value()].node;
            const auto& bone = frame->pose->bone_matrices[joint.value()];
            const auto animated = Math::Vec3(world * Math::Vec4(Math::Vec3(bone[3]), 1));
            float desired_offset = 0, desired_weight = 0;
            auto normal = up;
            if(grounded && settings.weight > 0) {
                auto hit = context.services.physics->raycast(animated + up * settings.probe_up,
                    -up * (settings.probe_up + settings.probe_down), {.ignored = actor.get_uuid()});
                if(!hit)
                    return Result<void, Error>::failure({hit.error()});
                if(hit.value()
                    && glm::dot(hit.value()->normal, up)
                           >= std::cos(glm::radians(settings.max_slope))) {
                    desired_offset =
                        std::clamp(hit.value()->position.y + settings.foot_height - animated.y,
                            -settings.max_offset, settings.max_offset);
                    // 摆动脚仍由动作控制，不把跳步和抬脚钉到地面。
                    const float lift = animated.y - hit.value()->position.y - settings.foot_height;
                    desired_weight =
                        settings.weight
                        * std::clamp(2 - std::max(lift, 0.f) / settings.plant_height, 0.f, 1.f);
                    normal = hit.value()->normal;
                }
            }
            auto& foot = state.feet[i];
            foot.offset = glm::mix(foot.offset, desired_offset, alpha);
            foot.weight = glm::mix(foot.weight, desired_weight, alpha);
            foot.normal = glm::normalize(glm::mix(foot.normal, normal, alpha));
            if(foot.weight < .0001f)
                continue;
            const auto target = Math::Vec3(inverse * Math::Vec4(animated + up * foot.offset, 1));
            const auto tilt =
                glm::inverse(orientation) * glm::rotation(up, foot.normal) * orientation;
            const auto basis = Math::Mat3(bone);
            const auto foot_orientation =
                glm::normalize(glm::quat_cast(basis / glm::length(basis[0])));
            output->ik.push_back({.tip = node,
                .target = target,
                .pole = Math::Vec3(bone[3]) + glm::normalize(settings.knee_forward),
                .rotation = glm::normalize(tilt * foot_orientation),
                .weight = foot.weight});
        }
        scene.set_animation_playback(model, std::move(output));
        return Result<void, Error>::success();
    }
    Result<void, Error> FootPlacementSystem::post_fixed_update(
        Scene& scene, const Context& context) {
        auto result = Result<void, Error>::success();
        scene.each<const FootPlacementComponent, const AnimatorComponent>(
            [&](Entity model, const FootPlacementComponent& settings,
                const AnimatorComponent& animator) {
                if(!result || !settings.enabled || !animator.enabled)
                    return;
                result = place(scene, model, settings, context, m_states[model.get_id()]);
            });
        std::erase_if(m_states, [&](const auto& entry) {
            const Entity model = scene.find_entity(entry.first);
            return !model || !model.has_component<FootPlacementComponent>()
                   || !model.get_component<FootPlacementComponent>().enabled
                   || !model.has_component<AnimatorComponent>()
                   || !model.get_component<AnimatorComponent>().enabled;
        });
        return result;
    }
    void FootPlacementSystem::on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept {
        m_states.clear();
    }
}
