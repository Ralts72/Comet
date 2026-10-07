#include "input/player_input_edit.h"

#include <algorithm>
#include <utility>

namespace Comet {
    namespace {
        InputActions::Binding composed_binding(
            const InputActions::Binding& binding, const InputOverrides::Binding& patch) {
            auto composed = binding;
            if(patch.control)
                composed.control = *patch.control;
            if(patch.scale)
                composed.scale = *patch.scale;
            if(patch.deadzone)
                composed.deadzone = *patch.deadzone;
            return composed;
        }
    }

    void PlayerInputEdit::reset(const InputActions& defaults, const InputOverrides& current,
        std::span<const Input::Key> reserved_keys) {
        m_defaults = defaults;
        m_draft = current;
        m_reserved_keys.assign(reserved_keys.begin(), reserved_keys.end());
        m_resolution = m_draft.resolve(m_defaults);
        m_request.reset();
        m_capture.reset();
        m_error.clear();
        m_waiting = false;
    }

    void PlayerInputEdit::clear() {
        m_draft = {};
        m_resolution = m_draft.resolve(m_defaults);
        m_request.reset();
        m_capture.reset();
        m_error.clear();
        m_waiting = false;
    }

    const InputOverrides::Action* PlayerInputEdit::action_patch(const Uuid id) const {
        const auto& actions = m_draft.actions();
        const auto found = std::ranges::find(actions, id, &InputOverrides::Action::id);
        return found == actions.end() ? nullptr : &*found;
    }

    InputOverrides::Binding PlayerInputEdit::binding_patch(
        const Uuid action, const Uuid binding) const {
        if(const auto* patch = action_patch(action)) {
            const auto found =
                std::ranges::find(patch->bindings, binding, &InputOverrides::Binding::id);
            if(found != patch->bindings.end())
                return *found;
        }
        return {.id = binding};
    }

    bool PlayerInputEdit::is_reserved(const InputActions::Control& control) const {
        const auto* key = std::get_if<Input::Key>(&control);
        return key && std::ranges::find(m_reserved_keys, *key) != m_reserved_keys.end();
    }

    const PlayerInputEdit::Action* PlayerInputEdit::find_action(const Uuid id) {
        const auto& actions = m_defaults.actions();
        const auto found = std::ranges::find(actions, id, &Action::id);
        if(found != actions.end()) {
            if(const auto* patch = action_patch(id); patch && patch->type != found->type) {
                m_error = "Restore this action before editing incompatible overrides.";
                return nullptr;
            }
            return &*found;
        }
        m_error = "Unknown input action ID";
        return nullptr;
    }

    const PlayerInputEdit::Binding* PlayerInputEdit::find_binding(
        const Action& action, const Uuid id) {
        const auto found = std::ranges::find(action.bindings, id, &Binding::id);
        if(found != action.bindings.end())
            return &*found;
        m_error = "Unknown input binding ID";
        return nullptr;
    }

    void PlayerInputEdit::commit(std::vector<InputOverrides::Action> actions) {
        auto candidate = InputOverrides::create(std::move(actions));
        if(!candidate) {
            m_error = candidate.error();
            return;
        }
        m_draft = std::move(candidate).value();
        m_resolution = m_draft.resolve(m_defaults);
        m_error.clear();
    }

    void PlayerInputEdit::restore_action(const Uuid id) {
        if(m_waiting)
            return;
        auto actions = m_draft.actions();
        std::erase_if(actions, [&](const auto& action) { return action.id == id; });
        commit(std::move(actions));
        m_capture.reset();
    }

    void PlayerInputEdit::restore_binding(const Uuid action, const Uuid binding) {
        if(m_waiting)
            return;
        m_capture.reset();
        auto actions = m_draft.actions();
        const auto found = std::ranges::find(actions, action, &InputOverrides::Action::id);
        if(found == actions.end()) {
            m_error.clear();
            return;
        }
        std::erase_if(found->bindings, [&](const auto& patch) { return patch.id == binding; });
        if(!found->disabled && found->bindings.empty())
            actions.erase(found);
        commit(std::move(actions));
    }

    void PlayerInputEdit::restore_all() {
        if(m_waiting)
            return;
        m_draft = {};
        m_resolution = m_draft.resolve(m_defaults);
        m_capture.reset();
        m_error.clear();
    }

    void PlayerInputEdit::disable_action(const Uuid id, const bool disabled) {
        if(m_waiting)
            return;
        const auto* action = find_action(id);
        if(!action)
            return;
        auto actions = m_draft.actions();
        auto found = std::ranges::find(actions, id, &InputOverrides::Action::id);
        if(found == actions.end()) {
            if(disabled)
                actions.push_back({id, action->type, true, {}});
        } else {
            found->disabled = disabled;
            if(!disabled && found->bindings.empty())
                actions.erase(found);
        }
        commit(std::move(actions));
        m_capture.reset();
    }

    void PlayerInputEdit::disable_binding(
        const Uuid action_id, const Uuid binding_id, const bool disabled) {
        if(m_waiting)
            return;
        const auto* action = find_action(action_id);
        const auto* binding = action ? find_binding(*action, binding_id) : nullptr;
        if(!binding)
            return;
        auto patch = binding_patch(action_id, binding_id);
        patch.disabled = disabled;
        // 启停保留失配字段，由 resolve 给出默认回退。
        commit_binding(*action, *binding, std::move(patch));
        m_capture.reset();
    }

    void PlayerInputEdit::commit_binding(
        const Action& action, const Binding& binding, InputOverrides::Binding patch) {
        if(!patch.disabled && !patch.control && !patch.scale && !patch.deadzone) {
            restore_binding(action.id, binding.id);
            return;
        }
        auto actions = m_draft.actions();
        auto found = std::ranges::find(actions, action.id, &InputOverrides::Action::id);
        if(found == actions.end()) {
            actions.push_back({action.id, action.type, false, {std::move(patch)}});
        } else {
            auto record =
                std::ranges::find(found->bindings, binding.id, &InputOverrides::Binding::id);
            if(record == found->bindings.end())
                found->bindings.push_back(std::move(patch));
            else
                *record = std::move(patch);
        }
        commit(std::move(actions));
    }

    void PlayerInputEdit::store_binding(
        const Action& action, const Binding& binding, InputOverrides::Binding patch) {
        const auto valid =
            InputActions::create({{action.name, action.type, {composed_binding(binding, patch)},
                                     action.context, action.id}},
                m_defaults.contexts());
        if(!valid) {
            m_error = valid.error();
            return;
        }
        commit_binding(action, binding, std::move(patch));
    }

    void PlayerInputEdit::change_control(
        const Uuid action_id, const Uuid binding_id, InputActions::Control control) {
        if(m_waiting)
            return;
        const auto* action = find_action(action_id);
        const auto* binding = action ? find_binding(*action, binding_id) : nullptr;
        if(!binding)
            return;
        if(is_reserved(control)) {
            m_error = "This key is reserved. Use another key.";
            return;
        }
        auto patch = binding_patch(action_id, binding_id);
        if(std::holds_alternative<Input::GamepadAxis>(control)) {
            if(!std::holds_alternative<Input::GamepadAxis>(
                   patch.control.value_or(binding->control)))
                patch.deadzone.reset();
        } else if(patch.deadzone.value_or(binding->deadzone) != 0) {
            patch.deadzone = 0;
            if(binding->deadzone == 0)
                patch.deadzone.reset();
        }
        patch.control = control;
        if(control == binding->control)
            patch.control.reset();
        store_binding(*action, *binding, std::move(patch));
    }

    void PlayerInputEdit::change_scale(
        const Uuid action_id, const Uuid binding_id, const float scale) {
        if(m_waiting)
            return;
        const auto* action = find_action(action_id);
        const auto* binding = action ? find_binding(*action, binding_id) : nullptr;
        if(!binding)
            return;
        auto patch = binding_patch(action_id, binding_id);
        patch.scale = scale;
        if(scale == binding->scale)
            patch.scale.reset();
        store_binding(*action, *binding, std::move(patch));
    }

    void PlayerInputEdit::change_deadzone(
        const Uuid action_id, const Uuid binding_id, const float deadzone) {
        if(m_waiting)
            return;
        const auto* action = find_action(action_id);
        const auto* binding = action ? find_binding(*action, binding_id) : nullptr;
        if(!binding)
            return;
        auto patch = binding_patch(action_id, binding_id);
        patch.deadzone = deadzone;
        if(deadzone == binding->deadzone)
            patch.deadzone.reset();
        store_binding(*action, *binding, std::move(patch));
    }

    void PlayerInputEdit::report_error(std::string error) {
        if(!m_waiting)
            m_error = std::move(error);
    }

    void PlayerInputEdit::start_capture(const Uuid action_id, const Uuid binding_id,
        const Input::Frame& input, const CaptureKind kind) {
        if(m_waiting)
            return;
        m_capture.reset();
        const auto* action = find_action(action_id);
        if(!action || !find_binding(*action, binding_id) || !input.focused)
            return;
        Capture capture{action_id, binding_id, input.interruption, input.serial, {}};
        if(kind == CaptureKind::GamepadButton) {
            capture.gamepad = input.first_connected_gamepad();
            if(!capture.gamepad) {
                m_error = "No gamepad connected.";
                return;
            }
            capture.gamepad_connection_revision =
                input.gamepads[*capture.gamepad].connection_revision;
        }
        m_capture = capture;
        m_error.clear();
    }

    void PlayerInputEdit::capture_input(const Input::Frame& input, const bool interaction_allowed) {
        if(!m_capture || m_waiting)
            return;
        if(!interaction_allowed || !input.focused || input.interruption != m_capture->interruption
            || input.serial < m_capture->serial) {
            m_capture.reset();
            return;
        }
        if(m_capture->gamepad
            && (input.first_connected_gamepad() != m_capture->gamepad
                || input.gamepads[*m_capture->gamepad].connection_revision
                       != m_capture->gamepad_connection_revision)) {
            m_capture.reset();
            return;
        }
        if(input.serial == m_capture->serial)
            return;
        m_capture->serial = input.serial;
        if(input.key(Input::Key::Escape).pressed) {
            m_capture.reset();
            return;
        }
        if(m_capture->gamepad) {
            const auto& buttons = input.gamepads[*m_capture->gamepad].buttons;
            for(std::size_t index = 0; index < buttons.size(); ++index) {
                if(buttons[index].pressed) {
                    change_control(m_capture->action, m_capture->binding,
                        static_cast<Input::GamepadButton>(index));
                    m_capture.reset();
                    return;
                }
            }
            return;
        }
        for(int index = int(Input::Key::Unknown) + 1; index < int(Input::Key::Count); ++index) {
            const auto key = static_cast<Input::Key>(index);
            if(input.key(key).pressed) {
                if(is_reserved(key)) {
                    m_error = "This key is reserved. Use another key.";
                    return;
                }
                change_control(m_capture->action, m_capture->binding, key);
                m_capture.reset();
                return;
            }
        }
    }

    void PlayerInputEdit::cancel_capture() {
        m_capture.reset();
    }

    void PlayerInputEdit::apply() {
        if(m_waiting)
            return;
        if(!m_resolution) {
            m_error = m_resolution.error();
            return;
        }
        m_request = m_draft;
        m_waiting = true;
        m_capture.reset();
        m_error.clear();
    }

    std::optional<InputOverrides> PlayerInputEdit::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    bool PlayerInputEdit::complete(const Result<void>& result) {
        if(!m_waiting)
            return false;
        m_waiting = false;
        m_request.reset();
        if(result)
            return true;
        m_error = result.error();
        return false;
    }
}
