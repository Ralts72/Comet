#include "project/input_widgets.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <imgui_internal.h>

namespace CometEditor::Ui {
    namespace {
        using Actions = Comet::InputActions;
        using BindingRelation = Actions::BindingRelation;

        template<typename Control, int First, int Last> constexpr auto make_controls() {
            std::array<Actions::Control, Last - First> controls{};
            for(int index = First; index < Last; ++index)
                controls[index - First] = static_cast<Control>(index);
            return controls;
        }

        const char* text(const Translations& translations, const char* english) {
            const auto found = translations.find(english);
            return found == translations.end() ? english : found->second.c_str();
        }

        const Actions::Context* action_context(
            const Actions& actions, const Actions::Action& action) {
            const auto& contexts = actions.contexts();
            const auto found = std::ranges::find(contexts, action.context, &Actions::Context::name);
            return found == contexts.end() ? nullptr : &*found;
        }

        const char* relation_name(const BindingRelation relation, const bool common) {
            switch(relation) {
                case BindingRelation::Shared:
                    if(common)
                        return "Shared (common action bypasses consumption)";
                    return "Shared";
                case BindingRelation::Consumes:
                    return "Consumes this control from the other action";
                case BindingRelation::ConsumedBy:
                    return "This control is consumed by the other action";
                case BindingRelation::Unrelated:
                    return "No overlapping bindings.";
            }
            return "Unknown";
        }

        void render_relationship(const Actions::Binding& binding, const Actions::Action& other,
            const Actions::Context* context, const Actions::Context* other_context,
            const BindingRelation relation, const Translations& translations) {
            const auto control = Actions::format_binding(binding);
            if(!control)
                return;
            const char* group = text(translations, "Common (Always Enabled)");
            if(other_context)
                group = other_context->name.c_str();
            ImGui::Separator();
            ImGui::TextWrapped("%s/%s: %s [%s]", control.value().source.data(),
                control.value().control.c_str(), other.name.c_str(), group);
            ImGui::TextWrapped(
                "%s", text(translations, relation_name(relation, !context || !other_context)));
            if(context && !context->enabled)
                ImGui::TextWrapped(
                    "%s %s", text(translations, "Initially disabled:"), context->name.c_str());
            if(other_context && other_context != context && !other_context->enabled)
                ImGui::TextWrapped("%s %s", text(translations, "Initially disabled:"),
                    other_context->name.c_str());
        }
    }

    const char* input_type_name(const Actions::Type type) {
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

    std::span<const std::string_view> input_sources(const Actions::Type type) {
        static constexpr std::array<std::string_view, 5> sources{
            "key", "mouse_button", "gamepad_button", "gamepad_axis", "motion"};
        const std::span<const std::string_view> all = sources;
        switch(type) {
            case Actions::Type::Button:
                return all.first(3);
            case Actions::Type::Axis:
                return all.first(4);
            case Actions::Type::Delta:
                return all.last(1);
        }
        return {};
    }

    std::span<const Actions::Control> input_controls(const std::string_view source) {
        using Input = Comet::Input;
        static constexpr auto keys =
            make_controls<Input::Key, int(Input::Key::Unknown) + 1, int(Input::Key::Count)>();
        static constexpr auto mouse_buttons =
            make_controls<Input::MouseButton, 0, int(Input::MouseButton::Count)>();
        static constexpr auto gamepad_buttons =
            make_controls<Input::GamepadButton, 0, int(Input::GamepadButton::Count)>();
        static constexpr auto gamepad_axes =
            make_controls<Input::GamepadAxis, 0, int(Input::GamepadAxis::Count)>();
        static constexpr auto motions =
            make_controls<Actions::Motion, 0, int(Actions::Motion::ScrollY) + 1>();
        if(source == "key")
            return keys;
        if(source == "mouse_button")
            return mouse_buttons;
        if(source == "gamepad_button")
            return gamepad_buttons;
        if(source == "gamepad_axis")
            return gamepad_axes;
        if(source == "motion")
            return motions;
        return {};
    }

    void render_binding_relationships(const Actions& actions, const std::size_t selected_action,
        const Translations& translations) {
        if(selected_action >= actions.actions().size())
            return;
        ImGui::TextWrapped(
            "%s", text(translations,
                      "Pairwise rules when both contexts are enabled; not current runtime state."));
        const auto& selected = actions.actions()[selected_action];
        const auto* context = action_context(actions, selected);
        bool found = false;
        for(auto binding = selected.bindings.begin(); binding != selected.bindings.end();
            ++binding) {
            if(std::any_of(selected.bindings.begin(), binding,
                   [&](const auto& previous) { return previous.control == binding->control; }))
                continue;
            for(std::size_t other_index = 0; other_index < actions.actions().size();
                ++other_index) {
                if(other_index == selected_action)
                    continue;
                const auto& other = actions.actions()[other_index];
                const auto* other_context = action_context(actions, other);
                for(const auto& other_binding : other.bindings) {
                    const auto relation =
                        Actions::compare_bindings(*binding, context, other_binding, other_context);
                    if(relation == BindingRelation::Unrelated)
                        continue;
                    render_relationship(
                        *binding, other, context, other_context, relation, translations);
                    found = true;
                    break;
                }
            }
        }
        if(found)
            ImGui::TextWrapped("%s",
                text(translations, "Other consuming contexts can still block non-common actions."));
        else
            ImGui::TextDisabled("%s", text(translations, "No overlapping bindings."));
    }

    void set_next_input_modal_bounds(const char* title, const bool opening,
        const ImVec2 initial_size, const ImVec2 minimum_size) {
        const auto* viewport = ImGui::GetMainViewport();
        const ImVec2 maximum(
            std::max(1.f, viewport->WorkSize.x - 16), std::max(1.f, viewport->WorkSize.y - 16));
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(minimum_size.x, maximum.x), std::min(minimum_size.y, maximum.y)),
            maximum);
        ImGui::SetNextWindowSize(
            ImVec2(std::min(initial_size.x, maximum.x), std::min(initial_size.y, maximum.y)),
            ImGuiCond_Appearing);
        const auto* previous = ImGui::FindWindowByName(title);
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
    }

    bool render_player_input_error(
        std::string& error, const bool close_requested, const Translations& translations) {
        const auto title =
            std::string(text(translations, "Input Settings Error")) + "###Input Settings Error";
        const bool already_open = ImGui::IsPopupOpen(title.c_str());
        if(error.empty() && !already_open)
            return false;
        if(!error.empty() && !already_open)
            ImGui::OpenPopup(title.c_str());

        set_next_input_modal_bounds(
            title.c_str(), !already_open, ImVec2(470, 220), ImVec2(320, 160));
        if(!ImGui::BeginPopupModal(title.c_str(), nullptr,
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                   | ImGuiWindowFlags_NoScrollWithMouse))
            return true;
        if(error.empty()) {
            ImGui::CloseCurrentPopup();
        } else {
            const auto height = std::max(
                1.f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing());
            ImGui::BeginChild("Content", ImVec2(0, height));
            if(!already_open)
                ImGui::SetScrollY(0);
            ImGui::TextWrapped("%s", error.c_str());
            ImGui::EndChild();
            const auto close = std::string(text(translations, "Close")) + "###Close";
            if(ImGui::Button(close.c_str()) || close_requested) {
                error.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
        return true;
    }
}
