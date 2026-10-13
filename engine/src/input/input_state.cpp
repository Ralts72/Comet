#include "input/input_state.h"

#include <cmath>

namespace Comet {
    const InputState::Action* InputState::action(std::string_view name) const {
        const auto found = m_actions.find(name);
        if(found == m_actions.end())
            return nullptr;
        return &found->second;
    }

    Result<InputState::Action> InputState::read_action(
        const std::string_view name, const Action::Type type, const bool required) const {
        const auto* value = action(name);
        if(!value) {
            if(required)
                return Result<Action>::failure("Unknown input action: " + std::string(name));
            return Result<Action>::success({.type = type});
        }
        if(value->type != type)
            return Result<Action>::failure("Input action has wrong type: " + std::string(name));
        if(!std::isfinite(value->value))
            return Result<Action>::failure("Input action must be finite: " + std::string(name));
        return Result<Action>::success(*value);
    }

    void InputState::clear_transients() {
        m_physical.clear_transients();
        for(auto& [name, action] : m_actions) {
            action.pressed = action.released = false;
            if(action.type == Action::Type::Delta)
                action.value = 0;
        }
    }
}
