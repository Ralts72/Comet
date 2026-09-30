#include "project/input_settings_panel.h"

#include "ui/language.h"
#include "ui/widgets.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <imgui.h>
#include <imgui_internal.h>
#include <string_view>

namespace CometEditor {
    namespace {
        using Type = Comet::InputActions::Type;

        const char* type_name(const Type type) {
            switch(type) {
                case Type::Button:
                    return "Button";
                case Type::Axis:
                    return "Axis";
                case Type::Delta:
                    return "Delta";
            }
            return "Unknown";
        }

        bool source_allowed(const Type type, const std::string_view source) {
            if(type == Type::Delta)
                return source == "motion";
            if(source == "motion")
                return false;
            return type == Type::Axis || source != "gamepad_axis";
        }

        std::optional<std::string> key_name(const ImGuiKey key) {
            if(key >= ImGuiKey_A && key <= ImGuiKey_Z)
                return std::string(1, char('A' + key - ImGuiKey_A));
            if(key >= ImGuiKey_0 && key <= ImGuiKey_9)
                return std::string(1, char('0' + key - ImGuiKey_0));
            if(key >= ImGuiKey_F1 && key <= ImGuiKey_F24)
                return "F" + std::to_string(key - ImGuiKey_F1 + 1);
            switch(key) {
                case ImGuiKey_Space:
                    return "Space";
                case ImGuiKey_Enter:
                    return "Enter";
                case ImGuiKey_Tab:
                    return "Tab";
                case ImGuiKey_Backspace:
                    return "Backspace";
                case ImGuiKey_Delete:
                    return "Delete";
                case ImGuiKey_Insert:
                    return "Insert";
                case ImGuiKey_Home:
                    return "Home";
                case ImGuiKey_End:
                    return "End";
                case ImGuiKey_PageUp:
                    return "PageUp";
                case ImGuiKey_PageDown:
                    return "PageDown";
                case ImGuiKey_UpArrow:
                    return "Up";
                case ImGuiKey_DownArrow:
                    return "Down";
                case ImGuiKey_LeftArrow:
                    return "Left";
                case ImGuiKey_RightArrow:
                    return "Right";
                case ImGuiKey_LeftShift:
                    return "LeftShift";
                case ImGuiKey_RightShift:
                    return "RightShift";
                case ImGuiKey_LeftCtrl:
                    return "LeftControl";
                case ImGuiKey_RightCtrl:
                    return "RightControl";
                case ImGuiKey_LeftAlt:
                    return "LeftAlt";
                case ImGuiKey_RightAlt:
                    return "RightAlt";
                case ImGuiKey_LeftSuper:
                    return "LeftSuper";
                case ImGuiKey_RightSuper:
                    return "RightSuper";
                default:
                    return std::nullopt;
            }
        }
    }

    InputSettingsPanel::InputSettingsPanel() : EditorPanel("Project Settings - Input") {
        set_visible(false);
    }

    void InputSettingsPanel::request(const Comet::InputActions& current) {
        if(is_open())
            return;
        m_actions.clear();
        m_contexts = current.contexts();
        for(const auto& action : current.actions()) {
            ActionDraft draft{action.name, action.type, {}, {}};
            if(!action.context.empty()) {
                const auto group = std::ranges::find(
                    m_contexts, action.context, &Comet::InputActions::Context::name);
                draft.context = std::distance(m_contexts.begin(), group);
            }
            for(const auto& binding : action.bindings) {
                const auto control = Comet::InputActions::format_binding(binding);
                if(control)
                    draft.bindings.push_back({std::string(control.value().source),
                        control.value().control, binding.scale, binding.deadzone});
            }
            m_actions.push_back(std::move(draft));
        }
        m_selected_action = m_actions.empty() ? std::nullopt : std::optional<std::size_t>{0};
        m_capturing.reset();
        m_request.reset();
        m_error.clear();
        set_visible(true);
    }

    Comet::Result<Comet::InputActions> InputSettingsPanel::build() const {
        using Result = Comet::Result<Comet::InputActions>;
        std::vector<Comet::InputActions::Action> actions;
        for(const auto& draft : m_actions) {
            Comet::InputActions::Action action{draft.name, draft.type, {}};
            if(draft.context)
                action.context = m_contexts[*draft.context].name;
            for(const auto& binding : draft.bindings) {
                auto parsed = Comet::InputActions::parse_binding(
                    binding.source, binding.control, binding.scale, binding.deadzone);
                if(!parsed)
                    return Result::failure(draft.name + ": " + parsed.error());
                action.bindings.push_back(std::move(parsed).value());
            }
            actions.push_back(std::move(action));
        }
        return Comet::InputActions::create(std::move(actions), m_contexts);
    }

    void InputSettingsPanel::render_contexts() {
        if(!ImGui::CollapsingHeader(Ui::label("Input Contexts").c_str()))
            return;
        ImGui::BeginChild("ContextList", ImVec2(0, 135), true);
        ImGui::TextWrapped(
            "%s", Ui::text("Contexts switch independently; common actions stay enabled."));
        for(std::size_t index = 0; index < m_contexts.size();) {
            auto& context = m_contexts[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(180.0f);
            Ui::input_text("##ContextName", context.name);
            ImGui::SameLine();
            ImGui::Checkbox(Ui::label("Initially Enabled").c_str(), &context.enabled);
            const bool used = std::ranges::any_of(
                m_actions, [&](const auto& action) { return action.context == index; });
            ImGui::SameLine();
            ImGui::BeginDisabled(used);
            const bool remove = ImGui::Button(Ui::label("Remove Context").c_str());
            ImGui::EndDisabled();
            ImGui::PopID();
            if(remove) {
                m_contexts.erase(m_contexts.begin() + index);
                for(auto& action : m_actions)
                    if(action.context && *action.context > index)
                        --*action.context;
            } else
                ++index;
        }
        ImGui::BeginDisabled(m_contexts.size() >= Comet::InputActions::MAX_CONTEXTS);
        if(ImGui::Button(Ui::label("Add Context").c_str())) {
            for(std::size_t number = 1; number <= Comet::InputActions::MAX_CONTEXTS; ++number) {
                const auto name = "context_" + std::to_string(number);
                if(std::ranges::none_of(
                       m_contexts, [&](const auto& context) { return context.name == name; })) {
                    m_contexts.push_back({name});
                    break;
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", Ui::text("Unassign actions before removing a context."));
        ImGui::EndChild();
    }

    void InputSettingsPanel::render_binding(
        const std::size_t action_index, const std::size_t binding_index) {
        auto& action = m_actions[action_index];
        auto& binding = action.bindings[binding_index];
        ImGui::PushID(static_cast<int>(binding_index));
        constexpr std::array<std::string_view, 5> sources{
            "key", "mouse_button", "gamepad_button", "gamepad_axis", "motion"};
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##Source", binding.source.c_str())) {
            for(const auto source : sources) {
                if(!source_allowed(action.type, source))
                    continue;
                if(ImGui::Selectable(source.data(), binding.source == source)) {
                    binding.source = source;
                    binding.control = source == "motion" ? "CursorX" : "";
                    binding.scale = 1;
                    binding.deadzone = 0;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        Ui::input_text("##Control", binding.control);
        ImGui::TableSetColumnIndex(2);
        if(binding.source == "key") {
            const bool capturing =
                m_capturing && *m_capturing == std::pair{action_index, binding_index};
            if(ImGui::Button(Ui::label(capturing ? "Press Key" : "Record Key").c_str())) {
                m_capturing = std::pair{action_index, binding_index};
                ImGui::ClearActiveID();
            }
        }
        bool shared = false;
        for(std::size_t other = 0; other < m_actions.size(); ++other) {
            if(other == action_index)
                continue;
            for(const auto& candidate : m_actions[other].bindings)
                shared |= candidate.source == binding.source && candidate.control == binding.control
                          && !binding.control.empty();
        }
        if(shared) {
            ImGui::SameLine();
            ImGui::TextDisabled("!");
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Ui::text("Also used by another action"));
        }
        ImGui::TableSetColumnIndex(3);
        if(action.type != Type::Button) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputFloat("##Scale", &binding.scale, 0, 0, "%.2f");
        }
        ImGui::TableSetColumnIndex(4);
        if(binding.source == "gamepad_axis") {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputFloat("##Deadzone", &binding.deadzone, 0, 0, "%.2f");
        }
        ImGui::TableSetColumnIndex(5);
        if(ImGui::Button(Ui::label("Remove").c_str())) {
            action.bindings.erase(action.bindings.begin() + binding_index);
            m_capturing.reset();
        }
        ImGui::PopID();
    }

    void InputSettingsPanel::render_action(const std::size_t index) {
        auto& action = m_actions[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::SetNextItemWidth(165.0f);
        Ui::input_text("##Name", action.name);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        if(ImGui::BeginCombo("##Type", Ui::text(type_name(action.type)))) {
            for(const auto type : {Type::Button, Type::Axis, Type::Delta}) {
                if(ImGui::Selectable(Ui::text(type_name(type)), action.type == type))
                    action.type = type;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if(ImGui::Button(Ui::label("Remove Action").c_str())) {
            m_actions.erase(m_actions.begin() + index);
            if(m_actions.empty())
                m_selected_action.reset();
            else
                m_selected_action = std::min(index, m_actions.size() - 1);
            m_capturing.reset();
            ImGui::PopID();
            return;
        }
        ImGui::SetNextItemWidth(220.0f);
        const char* context_name = Ui::text("Common (Always Enabled)");
        if(action.context)
            context_name = m_contexts[*action.context].name.c_str();
        if(ImGui::BeginCombo(Ui::label("Context").c_str(), context_name)) {
            if(ImGui::Selectable(Ui::text("Common (Always Enabled)"), !action.context))
                action.context.reset();
            for(std::size_t context = 0; context < m_contexts.size(); ++context)
                if(!m_contexts[context].name.empty()
                    && ImGui::Selectable(
                        m_contexts[context].name.c_str(), action.context == context))
                    action.context = context;
            ImGui::EndCombo();
        }
        if(ImGui::BeginTable(
               "Bindings", 6, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX)) {
            ImGui::TableSetupColumn(Ui::text("Source"), ImGuiTableColumnFlags_WidthFixed, 125);
            ImGui::TableSetupColumn(Ui::text("Control"), ImGuiTableColumnFlags_WidthFixed, 155);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 105);
            ImGui::TableSetupColumn(Ui::text("Multiplier"), ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn(Ui::text("Deadzone"), ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableHeadersRow();
            for(std::size_t binding = 0; binding < action.bindings.size();) {
                const auto old_size = action.bindings.size();
                render_binding(index, binding);
                if(action.bindings.size() == old_size)
                    ++binding;
            }
            ImGui::EndTable();
        }
        ImGui::BeginDisabled(action.bindings.size() >= Comet::InputActions::MAX_BINDINGS);
        if(ImGui::Button(Ui::label("Add Binding").c_str())) {
            BindingDraft binding{"key", ""};
            if(action.type == Type::Delta)
                binding = {"motion", "CursorX"};
            action.bindings.push_back(std::move(binding));
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::PopID();
    }

    void InputSettingsPanel::capture_key() {
        const auto owner = ImGui::GetID("KeyCapture");
        if(!m_capturing || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
            || (ImGui::GetActiveID() != 0 && ImGui::GetActiveID() != owner)) {
            m_capturing.reset();
            if(ImGui::GetActiveID() == owner)
                ImGui::ClearActiveID();
            return;
        }
        ImGui::SetActiveID(owner, ImGui::GetCurrentWindow());
        ImGui::SetActiveIdUsingAllKeyboardKeys();
        if(ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_capturing.reset();
            ImGui::SetKeyOwner(ImGuiKey_Escape, owner, ImGuiInputFlags_LockUntilRelease);
            ImGui::ClearActiveID();
            return;
        }
        for(int value = ImGuiKey_NamedKey_BEGIN; value < ImGuiKey_NamedKey_END; ++value) {
            const auto key = static_cast<ImGuiKey>(value);
            if(!ImGui::IsKeyPressed(key, false))
                continue;
            if(const auto name = key_name(key)) {
                const auto [action, binding] = *m_capturing;
                if(action < m_actions.size() && binding < m_actions[action].bindings.size())
                    m_actions[action].bindings[binding].control = *name;
                m_capturing.reset();
                ImGui::SetKeyOwner(key, owner, ImGuiInputFlags_LockUntilRelease);
                ImGui::ClearActiveID();
                return;
            }
        }
    }

    void InputSettingsPanel::render() {
        if(!is_open()) {
            m_capturing.reset();
            return;
        }
        bool open = true;
        ImGui::SetNextWindowSize(ImVec2(900, 540), ImGuiCond_Appearing);
        if(!ImGui::Begin(window_label().c_str(), &open)) {
            m_capturing.reset();
            capture_key();
            ImGui::End();
            set_visible(open);
            return;
        }
        ImGui::TextUnformatted(Ui::text("Project defaults; restart App or Play to apply."));
        render_contexts();
        ImGui::BeginChild("ActionList", ImVec2(205, -70), true);
        ImGui::TextUnformatted(Ui::text("Actions"));
        ImGui::Separator();
        for(std::size_t index = 0; index < m_actions.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            const auto& name = m_actions[index].name;
            if(ImGui::Selectable(name.empty() ? Ui::text("Unnamed Action") : name.c_str(),
                   m_selected_action == index)) {
                m_selected_action = index;
                m_capturing.reset();
            }
            ImGui::PopID();
        }
        ImGui::BeginDisabled(m_actions.size() >= Comet::InputActions::MAX_ACTIONS);
        if(ImGui::Button(Ui::label("Add Action").c_str())) {
            for(std::size_t number = 1; number <= Comet::InputActions::MAX_ACTIONS; ++number) {
                const auto name = "action_" + std::to_string(number);
                if(std::ranges::none_of(
                       m_actions, [&](const auto& action) { return action.name == name; })) {
                    m_actions.push_back({name, Type::Button, {}});
                    m_selected_action = m_actions.size() - 1;
                    break;
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ActionDetails", ImVec2(0, -70), true);
        if(m_selected_action && *m_selected_action < m_actions.size())
            render_action(*m_selected_action);
        else
            ImGui::TextDisabled("%s", Ui::text("Select an action"));
        ImGui::EndChild();
        if(ImGui::Button(Ui::label("Save").c_str())) {
            auto actions = build();
            if(actions) {
                m_request = std::move(actions).value();
                m_error.clear();
            } else {
                m_error = actions.error();
            }
        }
        ImGui::SameLine();
        if(ImGui::Button(Ui::label("Close").c_str())) {
            open = false;
            m_request.reset();
            m_capturing.reset();
            m_error.clear();
        }
        if(!m_error.empty())
            ImGui::TextWrapped("%s", m_error.c_str());
        if(!open)
            m_capturing.reset();
        capture_key();
        ImGui::End();
        set_visible(open);
    }

    std::optional<Comet::InputActions> InputSettingsPanel::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    void InputSettingsPanel::complete(const Comet::Result<void>& result) {
        m_error = result ? std::string{} : result.error();
    }
}
