#include "gameplay/character_controller.h"
#include "input/input_state.h"
#include "physics/physics_access.h"
#include "scene/entity.h"

#include <string>
#include <utility>

namespace Comet::Gameplay {
    Result<bool> CharacterController::update(
        Entity entity, const InputState& input, PhysicsAccess& physics, const bool enabled) const {
        const auto state = physics.get_character_state(entity);
        if(!state)
            return Result<bool>::failure("Character control needs an enabled Character Controller");

        Math::Vec2 direction{0};
        bool jump_requested = false;
        if(enabled && input.focused()) {
            const auto* horizontal = input.action(move_x);
            const auto* vertical = input.action(move_z);
            const auto* jumping = input.action(jump);
            for(const auto& [name, action] :
                {std::pair{move_x, horizontal}, {move_z, vertical}, {jump, jumping}}) {
                if(!action)
                    return Result<bool>::failure(
                        "Unknown character input action: " + std::string(name));
            }
            if(horizontal->type != InputState::Action::Type::Axis
                || vertical->type != InputState::Action::Type::Axis)
                return Result<bool>::failure("Character movement requires two axis actions");
            if(jumping->type != InputState::Action::Type::Button)
                return Result<bool>::failure("Character jump requires a button action");
            direction = {horizontal->value, vertical->value};
            jump_requested = jumping->pressed && state->grounded;
        }
        if(!physics.request_character_move(entity, direction, jump_requested))
            return Result<bool>::failure("Cannot submit character movement to the active runtime");
        return Result<bool>::success(jump_requested);
    }
}
