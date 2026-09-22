#include "input/input_state.h"

namespace Comet {
    const InputState::Action* InputState::action(std::string_view name) const {
        const auto found = m_actions.find(name);
        if(found == m_actions.end())
            return nullptr;
        return &found->second;
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
