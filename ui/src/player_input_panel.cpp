#include "player_input_panel.h"
#include "input_widgets.h"

#include <algorithm>
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

        float button_width(const char* caption) {
            return ImGui::CalcTextSize(caption).x + ImGui::GetStyle().FramePadding.x * 2;
        }

        float field_width(float width, const char* caption) {
            return width + ImGui::CalcTextSize(caption).x + ImGui::GetStyle().ItemInnerSpacing.x;
        }

        void set_field_width(float width, const char* caption) {
            const auto remaining = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(caption).x
                                   - ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::SetNextItemWidth(std::max(1.f, std::min(width, remaining)));
        }

        void same_line_if_fits(float width) {
            const auto right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            if(ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right)
                ImGui::SameLine();
        }

        float footer_height(const Text& translations) {
            const auto& style = ImGui::GetStyle();
            const auto available = std::max(1.f, ImGui::GetContentRegionAvail().x);
            float used = 0;
            int rows = 1;
            for(const auto caption : {"Apply", "Cancel", "Restore All"}) {
                const auto width = std::min(available, button_width(text(translations, caption)));
                if(used > 0 && used + style.ItemSpacing.x + width > available) {
                    ++rows;
                    used = 0;
                }
                used += (used > 0 ? style.ItemSpacing.x : 0) + width;
            }
            return rows * ImGui::GetFrameHeight() + (rows - 1) * style.ItemSpacing.y;
        }

        bool binding_rejected(
            const Overrides::Resolution& resolved, Comet::Uuid action, Comet::Uuid binding) {
            return std::ranges::any_of(resolved.issues, [&](const auto& issue) {
                return issue.action == action && (!issue.binding || issue.binding == binding);
            });
        }

        bool is_reserved(
            const Actions::Control& control, std::span<const Input::Key> reserved_keys) {
            const auto* key = std::get_if<Input::Key>(&control);
            return key && std::ranges::find(reserved_keys, *key) != reserved_keys.end();
        }

        std::optional<Actions::Control> first_control(
            std::string_view source, std::span<const Input::Key> reserved_keys) {
            const auto controls = input_controls(source);
            if(controls.empty())
                return std::nullopt;
            auto preferred = controls.front();
            if(source == "key")
                preferred = Input::Key::Space;
            if(!is_reserved(preferred, reserved_keys))
                return preferred;
            for(const auto& control : controls)
                if(!is_reserved(control, reserved_keys))
                    return control;
            return std::nullopt;
        }

        void render_reserved_keys(
            std::span<const Input::Key> reserved_keys, const Text& translations) {
            if(reserved_keys.empty())
                return;
            std::string names;
            for(const auto key : reserved_keys) {
                if(!names.empty())
                    names += ", ";
                const auto name = Actions::format_binding({key}).value();
                names += text(translations, name.control.c_str());
            }
            ImGui::TextWrapped("%s %s", text(translations, "Reserved keys:"), names.c_str());
        }

        std::optional<Actions::Control> control_choices(const Actions::Control& current,
            std::string_view source, std::span<const Input::Key> reserved_keys,
            const Text& translations) {
            std::optional<Actions::Control> chosen;
            for(const auto& value : input_controls(source)) {
                const auto name = Actions::format_binding({value}).value();
                const bool selected = current == value;
                ImGui::BeginDisabled(is_reserved(value, reserved_keys));
                if(ImGui::Selectable(label(translations, name.control.c_str()).c_str(), selected))
                    chosen = value;
                if(selected)
                    ImGui::SetItemDefaultFocus();
                ImGui::EndDisabled();
            }
            return chosen;
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

        void render_disabled_binding(const Actions::Action& action, const Actions::Binding& binding,
            const Overrides::Binding& patch, const Text& translations) {
            const auto retained = composed_binding(binding, patch);
            const auto name = Actions::format_binding(retained).value();
            ImGui::TextWrapped("%s %s / %s", text(translations, "Disabled binding (not active):"),
                text(translations, name.source.data()), text(translations, name.control.c_str()));
            if(action.type != Actions::Type::Button || patch.scale)
                ImGui::TextWrapped("%s: %.3f", text(translations, "Multiplier"), retained.scale);
            if(name.source == "gamepad_axis" || patch.deadzone)
                ImGui::TextWrapped("%s: %.3f", text(translations, "Deadzone"), retained.deadzone);
        }
    }

    void PlayerInputPanel::open(const Actions& defaults, const Overrides& current,
        std::span<const Input::Key> reserved_keys) {
        if(m_open)
            return;
        m_defaults = defaults;
        m_draft = current;
        m_reserved_keys.assign(reserved_keys.begin(), reserved_keys.end());
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
        if(found == actions.end()) {
            m_error.clear();
            return;
        }
        std::erase_if(found->bindings, [&](const auto& patch) { return patch.id == binding; });
        if(!found->disabled && found->bindings.empty())
            actions.erase(found);
        commit(std::move(actions));
        m_capture.reset();
    }

    void PlayerInputPanel::disable_action(const Action& action, const bool disabled) {
        auto actions = m_draft.actions();
        auto found = std::ranges::find(actions, action.id, &Overrides::Action::id);
        if(found == actions.end()) {
            if(disabled)
                actions.push_back({action.id, action.type, true, {}});
        } else {
            found->disabled = disabled;
            if(!disabled && found->bindings.empty())
                actions.erase(found);
        }
        commit(std::move(actions));
        m_capture.reset();
    }

    void PlayerInputPanel::disable_binding(
        const Action& action, const Binding& binding, const bool disabled) {
        auto patch = binding_patch(action.id, binding.id);
        patch.disabled = disabled;
        // 启停保留失配字段，由 resolve 给出默认回退，不当作一次字段编辑。
        commit_binding(action, binding, std::move(patch));
        m_capture.reset();
    }

    void PlayerInputPanel::commit_binding(
        const Action& action, const Binding& binding, Overrides::Binding patch) {
        if(!patch.disabled && !patch.control && !patch.scale && !patch.deadzone) {
            restore_binding(action.id, binding.id);
            return;
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

    void PlayerInputPanel::store_binding(
        const Action& action, const Binding& binding, Overrides::Binding patch) {
        const auto valid =
            Actions::create({{action.name, action.type, {composed_binding(binding, patch)},
                                action.context, action.id}},
                m_defaults.contexts());
        if(!valid) {
            m_error = valid.error();
            return;
        }
        commit_binding(action, binding, std::move(patch));
    }

    void PlayerInputPanel::change_control(
        const Action& action, const Binding& binding, Actions::Control control) {
        if(is_reserved(control, m_reserved_keys)) {
            m_error = "This key is reserved. Use another key.";
            return;
        }
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

    void PlayerInputPanel::start_capture(const Action& action, const Binding& binding,
        const Input::Frame& input, const bool gamepad_button) {
        m_capture.reset();
        if(!input.focused)
            return;
        Capture capture{action.id, binding.id, input.interruption, input.serial, {}};
        if(gamepad_button) {
            capture.gamepad = input.first_connected_gamepad();
            if(!capture.gamepad) {
                m_error = "No gamepad connected.";
                return;
            }
        }
        m_capture = capture;
        m_error.clear();
        ImGui::ClearActiveID();
    }

    void PlayerInputPanel::capture_input(const Input::Frame& input) {
        if(!m_capture)
            return;
        if(!input.focused || input.interruption != m_capture->interruption
            || input.serial < m_capture->serial
            || (m_capture->gamepad && input.first_connected_gamepad() != m_capture->gamepad)
            || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
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
        if(m_capture->gamepad) {
            const auto& buttons = input.gamepads[*m_capture->gamepad].buttons;
            for(std::size_t index = 0; index < buttons.size(); ++index) {
                if(buttons[index].pressed) {
                    change_control(*action, *binding, static_cast<Input::GamepadButton>(index));
                    m_capture.reset();
                    return;
                }
            }
            return;
        }
        for(int index = int(Input::Key::Unknown) + 1; index < int(Input::Key::Count); ++index) {
            const auto key = static_cast<Input::Key>(index);
            if(input.key(key).pressed) {
                if(is_reserved(key, m_reserved_keys)) {
                    m_error = "This key is reserved. Use another key.";
                    return;
                }
                change_control(*action, *binding, key);
                m_capture.reset();
                return;
            }
        }
    }

    void PlayerInputPanel::render_controls(const Action& action, const Binding& binding,
        const Binding& effective, const Input::Frame& input, const Text& translations) {
        const auto name = Actions::format_binding(effective).value();
        if(is_reserved(effective.control, m_reserved_keys))
            ImGui::TextWrapped(
                "%s", text(translations,
                          "This binding uses a reserved key and will not reach the game."));
        set_field_width(145, text(translations, "Source"));
        if(ImGui::BeginCombo(
               label(translations, "Source").c_str(), text(translations, name.source.data()))) {
            for(const auto source : input_sources(action.type)) {
                if(ImGui::Selectable(
                       label(translations, source.data()).c_str(), name.source == source)
                    && name.source != source) {
                    if(const auto control = first_control(source, m_reserved_keys))
                        change_control(action, binding, *control);
                    else
                        m_error = "No available controls for this source.";
                    m_capture.reset();
                }
            }
            ImGui::EndCombo();
        }
        same_line_if_fits(field_width(150, text(translations, "Control")));
        set_field_width(150, text(translations, "Control"));
        if(ImGui::BeginCombo(
               label(translations, "Control").c_str(), text(translations, name.control.c_str()))) {
            if(const auto chosen = control_choices(
                   effective.control, name.source, m_reserved_keys, translations)) {
                change_control(action, binding, *chosen);
                m_capture.reset();
            }
            ImGui::EndCombo();
        }
        if(name.source == "key" || name.source == "gamepad_button") {
            const bool capturing =
                m_capture && m_capture->action == action.id && m_capture->binding == binding.id;
            const char* caption = "Record Key";
            if(name.source == "gamepad_button")
                caption = "Record Button";
            if(capturing) {
                if(name.source == "gamepad_button")
                    caption = "Press Button";
                else
                    caption = "Press Key";
            }
            same_line_if_fits(button_width(text(translations, caption)));
            if(ImGui::Button(label(translations, caption).c_str()))
                start_capture(action, binding, input, name.source == "gamepad_button");
        }
        if(action.type != Actions::Type::Button) {
            auto scale = effective.scale;
            set_field_width(120, text(translations, "Multiplier"));
            if(ImGui::InputFloat(label(translations, "Multiplier").c_str(), &scale, 0, 0, "%.3f")) {
                auto changed = binding_patch(action.id, binding.id);
                changed.scale = scale;
                if(scale == binding.scale)
                    changed.scale.reset();
                store_binding(action, binding, std::move(changed));
            }
        }
        if(name.source == "gamepad_axis") {
            same_line_if_fits(field_width(120, text(translations, "Deadzone")));
            auto deadzone = effective.deadzone;
            set_field_width(120, text(translations, "Deadzone"));
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
        Overrides::Resolution& resolved, const Input::Frame& input, const Text& translations) {
        ImGui::PushID(binding.id.to_string().c_str());
        auto patch = binding_patch(action.id, binding.id);
        const auto* action_override = action_patch(action.id);
        const bool incompatible = action_override && action_override->type != action.type;
        bool changed = false;
        bool disabled = patch.disabled && !incompatible;
        if(ImGui::Checkbox(label(translations, "Disable Binding").c_str(), &disabled)) {
            disable_binding(action, binding, disabled);
            changed = true;
        }
        same_line_if_fits(button_width(text(translations, "Restore Binding")));
        if(ImGui::Button(label(translations, "Restore Binding").c_str())) {
            restore_binding(action.id, binding.id);
            changed = true;
        }
        if(changed) {
            auto updated = m_draft.resolve(m_defaults);
            if(updated)
                resolved = std::move(updated).value();
        }
        patch = binding_patch(action.id, binding.id);
        if(patch.disabled && !incompatible) {
            render_disabled_binding(action, binding, patch, translations);
            ImGui::Separator();
            ImGui::PopID();
            return;
        }
        const bool overridden = patch.control || patch.scale || patch.deadzone;
        const bool rejected = binding_rejected(resolved, action.id, binding.id);
        const char* status = "Inherits project default";
        if(rejected)
            status = "Override ignored; using project default";
        else if(overridden)
            status = "Personal override";
        ImGui::TextWrapped("%s", text(translations, status));
        const auto& effective_bindings = resolved.actions.actions()[m_selected_action].bindings;
        const auto effective = std::ranges::find(effective_bindings, binding.id, &Binding::id);
        ImGui::BeginDisabled(rejected);
        render_controls(action, binding,
            effective == effective_bindings.end() ? binding : *effective, input, translations);
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
        set_field_width(250, text(translations, "Action"));
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
        same_line_if_fits(ImGui::CalcTextSize(text(translations, input_type_name(action.type))).x);
        ImGui::TextDisabled("%s", text(translations, input_type_name(action.type)));
        const auto* patch = action_patch(action.id);
        bool incompatible = patch && patch->type != action.type;
        bool disabled = !incompatible && patch && patch->disabled;
        ImGui::BeginDisabled(incompatible);
        if(ImGui::Checkbox(label(translations, "Disable Action").c_str(), &disabled))
            disable_action(action, disabled);
        ImGui::EndDisabled();
        same_line_if_fits(button_width(text(translations, "Restore Action")));
        if(ImGui::Button(label(translations, "Restore Action").c_str()))
            restore_action(action.id);
        patch = action_patch(action.id);
        incompatible = patch && patch->type != action.type;
        disabled = !incompatible && patch && patch->disabled;
        if(incompatible)
            ImGui::TextWrapped("%s",
                text(translations, "Restore this action before editing incompatible overrides."));
        if(disabled) {
            ImGui::TextWrapped(
                "%s", text(translations, "Disabled; personal overrides are preserved."));
            return;
        }
        auto resolved = m_draft.resolve(m_defaults);
        if(!resolved)
            return;
        ImGui::BeginDisabled(incompatible);
        ImGui::BeginChild("Bindings", ImVec2(0, 190), true);
        for(const auto& binding : action.bindings)
            render_binding(action, binding, resolved.value(), input, translations);
        ImGui::EndChild();
        ImGui::EndDisabled();
    }

    void PlayerInputPanel::render_feedback(const Text& translations) {
        const auto resolved = m_draft.resolve(m_defaults);
        if(!resolved) {
            ImGui::TextWrapped("%s", text(translations, resolved.error().c_str()));
            return;
        }
        if(ImGui::CollapsingHeader(label(translations, "Binding Relationships").c_str(),
               ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("Relationships", ImVec2(0, 120), true);
            render_binding_relationships(resolved.value().actions, m_selected_action, translations);
            ImGui::EndChild();
        }
        render_diagnostics(resolved.value(), translations);
    }

    void PlayerInputPanel::render_diagnostics(
        const Overrides::Resolution& resolved, const Text& translations) {
        if(resolved.issues.empty())
            return;
        ImGui::TextWrapped(
            "%s", text(translations, "Incompatible overrides are preserved until restored."));
        ImGui::BeginChild("Diagnostics", ImVec2(0, 75), true);
        for(const auto& issue : resolved.issues) {
            ImGui::PushID(issue.action.to_string().c_str());
            ImGui::PushID(issue.binding.to_string().c_str());
            ImGui::TextWrapped("%s", text(translations, issue.message.c_str()));
            if(ImGui::Button(label(translations, "Remove Override").c_str())) {
                if(issue.binding)
                    restore_binding(issue.action, issue.binding);
                else
                    restore_action(issue.action);
            }
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled(
                "%s / %s", issue.action.to_string().c_str(), issue.binding.to_string().c_str());
            ImGui::PopTextWrapPos();
            ImGui::Separator();
            ImGui::PopID();
            ImGui::PopID();
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
        const bool opening = m_open_requested;
        if(m_open_requested) {
            ImGui::OpenPopup(title.c_str());
            m_open_requested = false;
        }
        const auto* viewport = ImGui::GetMainViewport();
        const ImVec2 maximum(
            std::max(1.f, viewport->WorkSize.x - 16), std::max(1.f, viewport->WorkSize.y - 16));
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(320.f, maximum.x), std::min(240.f, maximum.y)), maximum);
        ImGui::SetNextWindowSize(
            ImVec2(std::min(800.f, maximum.x), std::min(550.f, maximum.y)), ImGuiCond_Appearing);
        const auto* previous = ImGui::FindWindowByName(title.c_str());
        if(previous && !opening) {
            const ImVec2 size(
                std::min(previous->Size.x, maximum.x), std::min(previous->Size.y, maximum.y));
            const ImVec2 minimum(viewport->WorkPos.x + 8, viewport->WorkPos.y + 8);
            ImGui::SetNextWindowPos(
                ImVec2(std::clamp(previous->Pos.x, minimum.x, minimum.x + maximum.x - size.x),
                    std::clamp(previous->Pos.y, minimum.y, minimum.y + maximum.y - size.y)));
        } else {
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                        viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        }
        if(!ImGui::BeginPopupModal(title.c_str(), nullptr,
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                   | ImGuiWindowFlags_NoScrollWithMouse)) {
            if(m_close_requested)
                m_close_requested = false;
            return true;
        }
        if(m_open && !m_waiting) {
            const bool capturing = m_capture.has_value();
            capture_input(input);
            if(!capturing && input.focused && input.key(Input::Key::Escape).pressed)
                close();
        }
        if(m_open) {
            render_reserved_keys(m_reserved_keys, translations);
            if(!m_error.empty()) {
                const auto available = ImGui::GetContentRegionAvail().y
                                       - footer_height(translations)
                                       - ImGui::GetStyle().ItemSpacing.y * 2;
                const auto error_height = std::min(96.f, std::max(1.f, available * 0.35f));
                ImGui::BeginChild("Error", ImVec2(0, error_height), true);
                ImGui::TextWrapped("%s", text(translations, m_error.c_str()));
                ImGui::EndChild();
            }
            const auto body_height =
                std::max(1.f, ImGui::GetContentRegionAvail().y - footer_height(translations)
                                  - ImGui::GetStyle().ItemSpacing.y);
            ImGui::BeginDisabled(m_waiting);
            ImGui::BeginChild("Content", ImVec2(0, body_height));
            ImGui::TextWrapped("%s",
                text(translations,
                    "Player overrides only; project defaults are unchanged. Unedited fields inherit defaults."));
            render_actions(input, translations);
            if(m_capture) {
                const char* prompt = "Press a key; Escape cancels recording.";
                if(m_capture->gamepad)
                    prompt = "Press a gamepad button; Escape cancels recording.";
                ImGui::TextWrapped("%s", text(translations, prompt));
            }
            render_feedback(translations);
            ImGui::EndChild();
            if(ImGui::Button(label(translations, "Apply").c_str()))
                apply();
            same_line_if_fits(button_width(text(translations, "Cancel")));
            if(ImGui::Button(label(translations, "Cancel").c_str()))
                close();
            same_line_if_fits(button_width(text(translations, "Restore All")));
            if(ImGui::Button(label(translations, "Restore All").c_str())) {
                m_draft = {};
                m_capture.reset();
                m_error.clear();
            }
            ImGui::EndDisabled();
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
