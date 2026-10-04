#include "player_input_panel.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <imgui_internal.h>
#include <string_view>
#include <utility>

namespace CometUi {
    namespace {
        using Input = Comet::Input;
        using Actions = Comet::InputActions;
        using Overrides = Comet::InputOverrides;
        using Text = PlayerInputPanel::Text;

        const char* text(const Text& translations, const char* english) {
            const auto found = translations.find(english);
            return found == translations.end() ? english : found->second.c_str();
        }

        std::string label(const Text& translations, const char* english) {
            return std::string(text(translations, english)) + "###" + english;
        }

        const char* type_name(Actions::Type type) {
            switch(type) {
                case Actions::Type::Button:
                    return "Button";
                case Actions::Type::Axis:
                    return "Axis";
                case Actions::Type::Delta:
                    return "Delta";
            }
            return "Unknown";
        }

        bool source_allowed(Actions::Type type, std::string_view source) {
            if(type == Actions::Type::Delta)
                return source == "motion";
            if(source == "motion")
                return false;
            return type == Actions::Type::Axis || source != "gamepad_axis";
        }

        Actions::Control first_control(std::string_view source) {
            if(source == "mouse_button")
                return Input::MouseButton::Left;
            if(source == "gamepad_button")
                return Input::GamepadButton::South;
            if(source == "gamepad_axis")
                return Input::GamepadAxis::LeftX;
            if(source == "motion")
                return Actions::Motion::CursorX;
            return Input::Key::Space;
        }

        template<typename Control>
        std::optional<Actions::Control> control_options(
            const Actions::Control& current, int begin, int end, const Text& translations) {
            std::optional<Actions::Control> chosen;
            for(int index = begin; index < end; ++index) {
                const Actions::Control value = static_cast<Control>(index);
                const auto name = Actions::format_binding({value}).value();
                const bool selected = current == value;
                if(ImGui::Selectable(label(translations, name.control.c_str()).c_str(), selected))
                    chosen = value;
                if(selected)
                    ImGui::SetItemDefaultFocus();
            }
            return chosen;
        }

        std::optional<Actions::Control> control_choices(
            const Actions::Control& current, std::string_view source, const Text& translations) {
            if(source == "key")
                return control_options<Input::Key>(
                    current, int(Input::Key::Unknown) + 1, int(Input::Key::Count), translations);
            if(source == "mouse_button")
                return control_options<Input::MouseButton>(
                    current, 0, int(Input::MouseButton::Count), translations);
            if(source == "gamepad_button")
                return control_options<Input::GamepadButton>(
                    current, 0, int(Input::GamepadButton::Count), translations);
            if(source == "gamepad_axis")
                return control_options<Input::GamepadAxis>(
                    current, 0, int(Input::GamepadAxis::Count), translations);
            return control_options<Actions::Motion>(
                current, 0, int(Actions::Motion::ScrollY) + 1, translations);
        }

        Actions::Binding composed_binding(
            const Actions::Binding& binding, const Overrides::Binding& patch) {
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

    void PlayerInputPanel::open(const Actions& defaults, const Overrides& current) {
        if(m_open)
            return;
        m_defaults = defaults;
        m_draft = current;
        m_selected_action = 0;
        m_request.reset();
        m_capture.reset();
        m_error.clear();
        m_open = true;
        m_open_requested = true;
        m_close_requested = false;
        m_waiting = false;
    }

    void PlayerInputPanel::close() {
        if(m_open)
            m_close_requested = true;
        m_open = false;
        m_open_requested = false;
        m_request.reset();
        m_capture.reset();
        m_waiting = false;
        m_error.clear();
        m_draft = {};
    }

    const Overrides::Action* PlayerInputPanel::action_patch(const Comet::Uuid id) const {
        const auto& actions = m_draft.actions();
        const auto found = std::ranges::find(actions, id, &Overrides::Action::id);
        return found == actions.end() ? nullptr : &*found;
    }

    Overrides::Binding PlayerInputPanel::binding_patch(
        const Comet::Uuid action, const Comet::Uuid binding) const {
        const auto* patch = action_patch(action);
        if(patch) {
            const auto found = std::ranges::find(patch->bindings, binding, &Overrides::Binding::id);
            if(found != patch->bindings.end())
                return *found;
        }
        return {.id = binding};
    }

    void PlayerInputPanel::commit(std::vector<Overrides::Action> actions) {
        auto candidate = Overrides::create(std::move(actions));
        if(!candidate) {
            m_error = candidate.error();
            return;
        }
        m_draft = std::move(candidate).value();
        m_error.clear();
    }

    void PlayerInputPanel::restore_action(const Comet::Uuid id) {
        auto actions = m_draft.actions();
        std::erase_if(actions, [&](const auto& action) { return action.id == id; });
        commit(std::move(actions));
        m_capture.reset();
    }

    void PlayerInputPanel::restore_binding(const Comet::Uuid action, const Comet::Uuid binding) {
        auto actions = m_draft.actions();
        const auto found = std::ranges::find(actions, action, &Overrides::Action::id);
        if(found == actions.end())
            return;
        std::erase_if(found->bindings, [&](const auto& patch) { return patch.id == binding; });
        if(!found->disabled && found->bindings.empty())
            actions.erase(found);
        commit(std::move(actions));
        m_capture.reset();
    }

    void PlayerInputPanel::disable_action(const Action& action, const bool disabled) {
        if(!disabled) {
            restore_action(action.id);
            return;
        }
        auto actions = m_draft.actions();
        auto found = std::ranges::find(actions, action.id, &Overrides::Action::id);
        Overrides::Action patch{action.id, action.type, true, {}};
        if(found == actions.end())
            actions.push_back(std::move(patch));
        else
            *found = std::move(patch);
        commit(std::move(actions));
        m_capture.reset();
    }

    void PlayerInputPanel::store_binding(
        const Action& action, const Binding& binding, Overrides::Binding patch) {
        if(!patch.disabled && !patch.control && !patch.scale && !patch.deadzone) {
            restore_binding(action.id, binding.id);
            return;
        }
        if(!patch.disabled) {
            const auto valid =
                Actions::create({{action.name, action.type, {composed_binding(binding, patch)},
                                    action.context, action.id}},
                    m_defaults.contexts());
            if(!valid) {
                m_error = valid.error();
                return;
            }
        }
        auto actions = m_draft.actions();
        auto found = std::ranges::find(actions, action.id, &Overrides::Action::id);
        if(found == actions.end()) {
            actions.push_back({action.id, action.type, false, {std::move(patch)}});
        } else {
            auto record = std::ranges::find(found->bindings, binding.id, &Overrides::Binding::id);
            if(record == found->bindings.end())
                found->bindings.push_back(std::move(patch));
            else
                *record = std::move(patch);
        }
        commit(std::move(actions));
    }

    void PlayerInputPanel::change_control(
        const Action& action, const Binding& binding, Actions::Control control) {
        auto patch = binding_patch(action.id, binding.id);
        patch.control = control;
        if(control == binding.control)
            patch.control.reset();
        if(!std::holds_alternative<Input::GamepadAxis>(control)
            && patch.deadzone.value_or(binding.deadzone) != 0) {
            patch.deadzone = 0;
            if(binding.deadzone == 0)
                patch.deadzone.reset();
        }
        store_binding(action, binding, std::move(patch));
    }

    void PlayerInputPanel::capture_key(const Input::Frame& input) {
        if(!m_capture)
            return;
        if(!input.focused || input.interruption != m_capture->interruption
            || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            m_capture.reset();
            return;
        }
        if(input.key(Input::Key::Escape).pressed) {
            m_capture.reset();
            return;
        }
        const auto& actions = m_defaults.actions();
        const auto action = std::ranges::find(actions, m_capture->action, &Action::id);
        if(action == actions.end()) {
            m_capture.reset();
            return;
        }
        const auto binding = std::ranges::find(action->bindings, m_capture->binding, &Binding::id);
        if(binding == action->bindings.end()) {
            m_capture.reset();
            return;
        }
        for(int index = int(Input::Key::Unknown) + 1; index < int(Input::Key::Count); ++index) {
            const auto key = static_cast<Input::Key>(index);
            if(input.key(key).pressed) {
                change_control(*action, *binding, key);
                m_capture.reset();
                return;
            }
        }
    }

    void PlayerInputPanel::render_controls(const Action& action, const Binding& binding,
        const Overrides::Binding& patch, const Input::Frame& input, const Text& translations) {
        const auto composed = composed_binding(binding, patch);
        const auto name = Actions::format_binding(composed).value();
        ImGui::SetNextItemWidth(145);
        if(ImGui::BeginCombo(
               label(translations, "Source").c_str(), text(translations, name.source.data()))) {
            constexpr std::array sources{
                "key", "mouse_button", "gamepad_button", "gamepad_axis", "motion"};
            for(const auto source : sources) {
                if(source_allowed(action.type, source)
                    && ImGui::Selectable(label(translations, source).c_str(), name.source == source)
                    && name.source != source) {
                    change_control(action, binding, first_control(source));
                    m_capture.reset();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        if(ImGui::BeginCombo(
               label(translations, "Control").c_str(), text(translations, name.control.c_str()))) {
            if(const auto chosen = control_choices(composed.control, name.source, translations)) {
                change_control(action, binding, *chosen);
                m_capture.reset();
            }
            ImGui::EndCombo();
        }
        if(name.source == "key") {
            ImGui::SameLine();
            const bool capturing =
                m_capture && m_capture->action == action.id && m_capture->binding == binding.id;
            const auto caption = capturing ? "Press Key" : "Record Key";
            if(ImGui::Button(label(translations, caption).c_str())) {
                m_capture = Capture{action.id, binding.id, input.interruption};
                ImGui::ClearActiveID();
            }
        }
        if(action.type != Actions::Type::Button) {
            auto scale = composed.scale;
            ImGui::SetNextItemWidth(120);
            if(ImGui::InputFloat(label(translations, "Multiplier").c_str(), &scale, 0, 0, "%.3f")) {
                auto changed = binding_patch(action.id, binding.id);
                changed.scale = scale;
                if(scale == binding.scale)
                    changed.scale.reset();
                store_binding(action, binding, std::move(changed));
            }
        }
        if(name.source == "gamepad_axis") {
            ImGui::SameLine();
            auto deadzone = composed.deadzone;
            ImGui::SetNextItemWidth(120);
            if(ImGui::InputFloat(
                   label(translations, "Deadzone").c_str(), &deadzone, 0, 0, "%.3f")) {
                auto changed = binding_patch(action.id, binding.id);
                changed.deadzone = deadzone;
                if(deadzone == binding.deadzone)
                    changed.deadzone.reset();
                store_binding(action, binding, std::move(changed));
            }
        }
    }

    void PlayerInputPanel::render_binding(const Action& action, const Binding& binding,
        const Input::Frame& input, const Text& translations) {
        ImGui::PushID(binding.id.to_string().c_str());
        auto patch = binding_patch(action.id, binding.id);
        bool disabled = patch.disabled;
        if(ImGui::Checkbox(label(translations, "Disable Binding").c_str(), &disabled)) {
            if(disabled)
                store_binding(action, binding, {.id = binding.id, .disabled = true});
            else
                restore_binding(action.id, binding.id);
            m_capture.reset();
        }
        ImGui::SameLine();
        if(ImGui::Button(label(translations, "Restore Binding").c_str()))
            restore_binding(action.id, binding.id);
        patch = binding_patch(action.id, binding.id);
        ImGui::BeginDisabled(patch.disabled);
        render_controls(action, binding, patch, input, translations);
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::PopID();
    }

    void PlayerInputPanel::render_actions(const Input::Frame& input, const Text& translations) {
        const auto& actions = m_defaults.actions();
        if(actions.empty()) {
            ImGui::TextDisabled("%s", text(translations, "No input actions."));
            return;
        }
        ImGui::SetNextItemWidth(250);
        if(ImGui::BeginCombo(
               label(translations, "Action").c_str(), actions[m_selected_action].name.c_str())) {
            for(std::size_t index = 0; index < actions.size(); ++index) {
                ImGui::PushID(actions[index].id.to_string().c_str());
                if(ImGui::Selectable(actions[index].name.c_str(), m_selected_action == index)) {
                    m_selected_action = index;
                    m_capture.reset();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        const auto& action = actions[m_selected_action];
        ImGui::SameLine();
        ImGui::TextDisabled("%s", text(translations, type_name(action.type)));
        const auto* patch = action_patch(action.id);
        const bool incompatible = patch && patch->type != action.type;
        bool disabled = patch && patch->disabled;
        ImGui::BeginDisabled(incompatible);
        if(ImGui::Checkbox(label(translations, "Disable Action").c_str(), &disabled))
            disable_action(action, disabled);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button(label(translations, "Restore Action").c_str()))
            restore_action(action.id);
        if(incompatible)
            ImGui::TextWrapped("%s",
                text(translations, "Restore this action before editing incompatible overrides."));
        ImGui::BeginDisabled(incompatible || disabled);
        ImGui::BeginChild("Bindings", ImVec2(0, 190), true);
        for(const auto& binding : action.bindings)
            render_binding(action, binding, input, translations);
        ImGui::EndChild();
        ImGui::EndDisabled();
    }

    void PlayerInputPanel::render_diagnostics(const Text& translations) const {
        const auto resolved = m_draft.resolve(m_defaults);
        if(!resolved) {
            ImGui::TextWrapped("%s", text(translations, resolved.error().c_str()));
            return;
        }
        if(resolved.value().issues.empty())
            return;
        ImGui::TextWrapped(
            "%s", text(translations, "Incompatible overrides are preserved until restored."));
        ImGui::BeginChild("Diagnostics", ImVec2(0, 75), true);
        for(const auto& issue : resolved.value().issues) {
            ImGui::TextWrapped("%s", text(translations, issue.message.c_str()));
            ImGui::TextDisabled(
                "%s / %s", issue.action.to_string().c_str(), issue.binding.to_string().c_str());
        }
        ImGui::EndChild();
    }

    void PlayerInputPanel::apply() {
        const auto resolved = m_draft.resolve(m_defaults);
        if(!resolved) {
            m_error = resolved.error();
            return;
        }
        m_request = m_draft;
        m_waiting = true;
        m_capture.reset();
        m_error.clear();
    }

    bool PlayerInputPanel::render(const Input::Frame& input, const Text& translations) {
        if(!m_open && !m_close_requested)
            return false;
        const auto title = label(translations, "Player Input");
        if(m_open_requested) {
            ImGui::OpenPopup(title.c_str());
            m_open_requested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(800, 550), ImGuiCond_Appearing);
        if(!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            if(m_close_requested)
                m_close_requested = false;
            return true;
        }
        if(m_open && !m_waiting) {
            const bool capturing = m_capture.has_value();
            capture_key(input);
            if(!capturing && input.focused && input.key(Input::Key::Escape).pressed)
                close();
        }
        if(m_open) {
            ImGui::TextWrapped("%s",
                text(translations,
                    "Player overrides only; project defaults are unchanged. Unedited fields inherit defaults."));
            ImGui::BeginDisabled(m_waiting);
            render_actions(input, translations);
            render_diagnostics(translations);
            if(m_capture)
                ImGui::TextWrapped(
                    "%s", text(translations, "Press a key; Escape cancels recording."));
            if(ImGui::Button(label(translations, "Apply").c_str()))
                apply();
            ImGui::SameLine();
            if(ImGui::Button(label(translations, "Cancel").c_str()))
                close();
            ImGui::SameLine();
            if(ImGui::Button(label(translations, "Restore All").c_str())) {
                m_draft = {};
                m_capture.reset();
                m_error.clear();
            }
            ImGui::EndDisabled();
            if(!m_error.empty())
                ImGui::TextWrapped("%s", text(translations, m_error.c_str()));
        }
        if(m_close_requested) {
            ImGui::CloseCurrentPopup();
            m_close_requested = false;
        }
        ImGui::EndPopup();
        return true;
    }

    std::optional<Overrides> PlayerInputPanel::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    void PlayerInputPanel::complete(const Comet::Result<void>& result) {
        if(!m_waiting)
            return;
        m_waiting = false;
        if(result)
            close();
        else
            m_error = result.error();
    }
}
