#include "gameplay/character_animation.h"
#include "gameplay/character_controller.h"
#include "scene/scene.h"
#include "physics/physics_access.h"
#include <algorithm>
#include <array>

namespace Comet::Gameplay {
    void CharacterAnimationSystem::update_parameters(
        Scene& scene, const RuntimeServices& services, std::optional<bool> fixed) {
        scene.each<const CharacterAnimationComponent, const AnimatorComponent>(
            [&](Entity entity, const CharacterAnimationComponent& driver,
                const AnimatorComponent& animator) {
                if(!animator.enabled || !animator.controller)
                    return;
                const bool root_motion = entity.has_component<RootMotionComponent>()
                                         && entity.get_component<RootMotionComponent>().enabled;
                const bool foot_placement =
                    entity.has_component<FootPlacementComponent>()
                    && entity.get_component<FootPlacementComponent>().enabled;
                if(fixed && *fixed != (root_motion || foot_placement))
                    return;
                CharacterController::State state;
                state.grounded = true;
                if(auto actor = scene.find_entity(driver.character); actor && services.physics) {
                    if(auto current = CharacterController::get_state(actor, *services.physics);
                        current)
                        state = current.value();
                    if(root_motion) {
                        const auto current = services.physics->get_character_state(actor);
                        if(current)
                            state.speed = Math::length(current->desired_velocity);
                    }
                }
                const std::array<std::pair<std::string, ParameterValue>, 4> values{
                    {{"speed", state.speed}, {"grounded", state.grounded},
                        {"rising", state.motion == CharacterController::Motion::Jumping},
                        {"crouched", state.crouched}}};
                const bool changed = std::ranges::any_of(values, [&](const auto& value) {
                    const auto found = animator.parameters.find(value.first);
                    return found == animator.parameters.end() || found->second != value.second;
                });
                if(changed)
                    entity.edit_component<AnimatorComponent>([&](auto& target) {
                        for(const auto& [name, value] : values)
                            target.parameters.insert_or_assign(name, value);
                    });
            });
    }
    Result<void, Error> CharacterAnimationSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices& services) {
        update_parameters(scene, services);
        return Result<void, Error>::success();
    }
    Result<void, Error> CharacterAnimationSystem::update(Scene& scene, const Context& context) {
        update_parameters(scene, context.services, false);
        return Result<void, Error>::success();
    }
    Result<void, Error> CharacterAnimationSystem::fixed_update(
        Scene& scene, const Context& context) {
        update_parameters(scene, context.services, true);
        return Result<void, Error>::success();
    }
}
