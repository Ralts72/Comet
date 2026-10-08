#include "project/player_input_panel.h"
#include "project/input_widgets.h"
#include "ui/widgets.h"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <string_view>
#include <utility>

namespace CometEditor {
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

        std::optional<Actions::Control> first_control(std::string_view source,
            const Actions::Control& default_control, std::span<const Input::Key> reserved_keys) {
            const auto controls = Ui::input_controls(source);
            if(controls.empty())
                return std::nullopt;
            if(std::ranges::find(controls, default_control) != controls.end()
                && !is_reserved(default_control, reserved_keys))
                return default_control;
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
            for(const auto& value : Ui::input_controls(source)) {
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
        m_edit.reset(defaults, current, reserved_keys);
        m_selected_action = 0;
        m_action_filter.clear();
        m_open = true;
        m_open_requested = true;
        m_close_requested = false;
    }

    void PlayerInputPanel::close() {
        if(m_open)
            m_close_requested = true;
        m_open = false;
        m_open_requested = false;
        m_action_filter.clear();
        m_edit.clear();
    }

    void PlayerInputPanel::render_controls(const Action& action, const Binding& binding,
        const Binding& effective, const Input::Frame& input, const Text& translations) {
        const auto name = Actions::format_binding(effective).value();
        if(m_edit.is_reserved(effective.control))
            ImGui::TextWrapped(
                "%s", text(translations,
                          "This binding uses a reserved key and will not reach the game."));
        set_field_width(145, text(translations, "Source"));
        if(ImGui::BeginCombo(
               label(translations, "Source").c_str(), text(translations, name.source.data()))) {
            for(const auto source : Ui::input_sources(action.type)) {
                if(ImGui::Selectable(
                       label(translations, source.data()).c_str(), name.source == source)
                    && name.source != source) {
                    if(const auto control =
                            first_control(source, binding.control, m_edit.reserved_keys()))
                        m_edit.change_control(action.id, binding.id, *control);
                    else
                        m_edit.report_error("No available controls for this source.");
                    m_edit.cancel_capture();
                }
            }
            ImGui::EndCombo();
        }
        same_line_if_fits(field_width(150, text(translations, "Control")));
        set_field_width(150, text(translations, "Control"));
        if(ImGui::BeginCombo(
               label(translations, "Control").c_str(), text(translations, name.control.c_str()))) {
            if(const auto chosen = control_choices(
                   effective.control, name.source, m_edit.reserved_keys(), translations)) {
                m_edit.change_control(action.id, binding.id, *chosen);
                m_edit.cancel_capture();
            }
            ImGui::EndCombo();
        }
        using CaptureKind = Comet::PlayerInputEdit::CaptureKind;
        if(name.source == "key")
            render_capture_button(action, binding, CaptureKind::Keyboard, input, translations);
        else if(name.source == "gamepad_button")
            render_capture_button(action, binding, CaptureKind::GamepadButton, input, translations);
        if(action.type != Actions::Type::Button) {
            auto scale = effective.scale;
            set_field_width(120, text(translations, "Multiplier"));
            if(ImGui::InputFloat(label(translations, "Multiplier").c_str(), &scale, 0, 0, "%.3f")) {
                m_edit.change_scale(action.id, binding.id, scale);
            }
        }
        if(name.source == "gamepad_axis") {
            same_line_if_fits(field_width(120, text(translations, "Deadzone")));
            auto deadzone = effective.deadzone;
            set_field_width(120, text(translations, "Deadzone"));
            if(ImGui::InputFloat(
                   label(translations, "Deadzone").c_str(), &deadzone, 0, 0, "%.3f")) {
                m_edit.change_deadzone(action.id, binding.id, deadzone);
            }
        }
    }

    void PlayerInputPanel::render_capture_button(const Action& action, const Binding& binding,
        const Comet::PlayerInputEdit::CaptureKind kind, const Input::Frame& input,
        const Text& translations) {
        const auto& capture = m_edit.capture();
        const bool capturing =
            capture && capture->action == action.id && capture->binding == binding.id;
        const char* caption = capturing ? "Press Key" : "Record Key";
        if(kind == Comet::PlayerInputEdit::CaptureKind::GamepadButton)
            caption = capturing ? "Press Button" : "Record Button";
        same_line_if_fits(button_width(text(translations, caption)));
        if(!ImGui::Button(label(translations, caption).c_str()))
            return;
        m_edit.start_capture(action.id, binding.id, input, kind);
        if(m_edit.capture())
            ImGui::ClearActiveID();
    }

    void PlayerInputPanel::render_binding(const Action& action, const Binding& binding,
        Overrides::Resolution& resolved, const Input::Frame& input, const Text& translations) {
        ImGui::PushID(binding.id.to_string().c_str());
        auto patch = m_edit.binding_patch(action.id, binding.id);
        const auto* action_override = m_edit.action_patch(action.id);
        const bool incompatible = action_override && action_override->type != action.type;
        bool changed = false;
        bool disabled = patch.disabled && !incompatible;
        if(ImGui::Checkbox(label(translations, "Disable Binding").c_str(), &disabled)) {
            m_edit.disable_binding(action.id, binding.id, disabled);
            changed = true;
        }
        same_line_if_fits(button_width(text(translations, "Restore Binding")));
        if(ImGui::Button(label(translations, "Restore Binding").c_str())) {
            m_edit.restore_binding(action.id, binding.id);
            changed = true;
        }
        if(changed) {
            auto updated = m_edit.resolution();
            if(updated)
                resolved = std::move(updated).value();
        }
        patch = m_edit.binding_patch(action.id, binding.id);
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
        const auto& effective_binding =
            effective == effective_bindings.end() ? binding : *effective;
        render_controls(action, binding, effective_binding, input, translations);
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::PopID();
    }

    void PlayerInputPanel::render_action_selector(const Text& translations) {
        const auto& actions = m_edit.defaults().actions();
        set_field_width(250, text(translations, "Filter Actions"));
        Ui::input_text(label(translations, "Filter Actions").c_str(), m_action_filter);
        if(!m_action_filter.empty()) {
            same_line_if_fits(button_width(text(translations, "Clear Filter")));
            if(ImGui::Button(label(translations, "Clear Filter").c_str()))
                m_action_filter.clear();
        }
        set_field_width(250, text(translations, "Action"));
        if(ImGui::BeginCombo(
               label(translations, "Action").c_str(), actions[m_selected_action].name.c_str())) {
            bool found = false;
            for(std::size_t index = 0; index < actions.size(); ++index) {
                if(!m_action_filter.empty()
                    && !ImStristr(
                        actions[index].name.c_str(), nullptr, m_action_filter.c_str(), nullptr))
                    continue;
                found = true;
                ImGui::PushID(actions[index].id.to_string().c_str());
                if(ImGui::Selectable(actions[index].name.c_str(), m_selected_action == index)) {
                    m_selected_action = index;
                    m_edit.cancel_capture();
                }
                ImGui::PopID();
            }
            if(!found)
                ImGui::TextDisabled("%s", text(translations, "No matching actions."));
            ImGui::EndCombo();
        }
    }

    void PlayerInputPanel::render_actions(const Input::Frame& input, const Text& translations) {
        const auto& actions = m_edit.defaults().actions();
        if(actions.empty()) {
            ImGui::TextDisabled("%s", text(translations, "No input actions."));
            return;
        }
        render_action_selector(translations);
        const auto& action = actions[m_selected_action];
        same_line_if_fits(
            ImGui::CalcTextSize(text(translations, Ui::input_type_name(action.type))).x);
        ImGui::TextDisabled("%s", text(translations, Ui::input_type_name(action.type)));
        const auto* patch = m_edit.action_patch(action.id);
        bool incompatible = patch && patch->type != action.type;
        bool disabled = !incompatible && patch && patch->disabled;
        ImGui::BeginDisabled(incompatible);
        if(ImGui::Checkbox(label(translations, "Disable Action").c_str(), &disabled))
            m_edit.disable_action(action.id, disabled);
        ImGui::EndDisabled();
        same_line_if_fits(button_width(text(translations, "Restore Action")));
        if(ImGui::Button(label(translations, "Restore Action").c_str()))
            m_edit.restore_action(action.id);
        patch = m_edit.action_patch(action.id);
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
        auto resolved = m_edit.resolution();
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
        const auto resolved = m_edit.resolution();
        if(!resolved) {
            ImGui::TextWrapped("%s", text(translations, resolved.error().c_str()));
            return;
        }
        if(ImGui::CollapsingHeader(label(translations, "Binding Relationships").c_str(),
               ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("Relationships", ImVec2(0, 120), true);
            Ui::render_binding_relationships(
                resolved.value().actions, m_selected_action, translations);
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
                    m_edit.restore_binding(issue.action, issue.binding);
                else
                    m_edit.restore_action(issue.action);
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

    void PlayerInputPanel::render_error(const Text& translations) {
        if(m_edit.error().empty())
            return;
        const auto available = ImGui::GetContentRegionAvail().y - footer_height(translations)
                               - ImGui::GetStyle().ItemSpacing.y * 2;
        const auto error_height = std::min(96.f, std::max(1.f, available * 0.35f));
        ImGui::BeginChild("Error", ImVec2(0, error_height), true);
        ImGui::TextWrapped("%s", text(translations, m_edit.error().c_str()));
        ImGui::EndChild();
    }

    void PlayerInputPanel::render_content(const Input::Frame& input, const Text& translations) {
        const auto body_height =
            std::max(1.f, ImGui::GetContentRegionAvail().y - footer_height(translations)
                              - ImGui::GetStyle().ItemSpacing.y);
        ImGui::BeginChild("Content", ImVec2(0, body_height));
        ImGui::TextWrapped("%s",
            text(translations,
                "Player overrides only; project defaults are unchanged. Unedited fields inherit defaults."));
        render_actions(input, translations);
        if(!m_edit.waiting()) {
            const bool editing_text = ImGui::GetInputTextState(ImGui::GetActiveID());
            const bool panel_focused =
                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            m_edit.capture_input(input, !editing_text && panel_focused);
        }
        if(m_edit.capture()) {
            const char* prompt = "Press a key; Escape cancels recording.";
            if(m_edit.capture()->gamepad)
                prompt = "Press a gamepad button; Escape cancels recording.";
            ImGui::TextWrapped("%s", text(translations, prompt));
        }
        render_feedback(translations);
        ImGui::EndChild();
    }

    void PlayerInputPanel::render_footer(const Text& translations) {
        if(ImGui::Button(label(translations, "Apply").c_str()))
            m_edit.apply();
        same_line_if_fits(button_width(text(translations, "Cancel")));
        if(ImGui::Button(label(translations, "Cancel").c_str()))
            close();
        same_line_if_fits(button_width(text(translations, "Restore All")));
        if(ImGui::Button(label(translations, "Restore All").c_str()))
            m_edit.restore_all();
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
        Ui::set_next_input_modal_bounds(title.c_str(), opening, ImVec2(800, 550), ImVec2(320, 240));
        if(!ImGui::BeginPopupModal(title.c_str(), nullptr,
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                   | ImGuiWindowFlags_NoScrollWithMouse)) {
            m_close_requested = false;
            return true;
        }
        if(m_open && !m_edit.waiting()) {
            const bool capturing = m_edit.capture().has_value();
            // Enter 可能在绘制数值框时结束编辑，先保留其本帧输入归属。
            if(ImGui::GetInputTextState(ImGui::GetActiveID()))
                m_edit.cancel_capture();
            if(!capturing && input.focused && input.key(Input::Key::Escape).pressed)
                close();
        }
        if(m_open) {
            render_reserved_keys(m_edit.reserved_keys(), translations);
            render_error(translations);
            ImGui::BeginDisabled(m_edit.waiting());
            render_content(input, translations);
            render_footer(translations);
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
        return m_edit.take_request();
    }

    void PlayerInputPanel::complete(const Comet::Result<void>& result) {
        if(m_edit.complete(result))
            close();
    }
}
