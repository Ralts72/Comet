#include "input_widgets.h"
#include "input/input_actions.h"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>

namespace CometUi {
    namespace {
        using Actions = Comet::InputActions;
        using BindingRelation = Actions::BindingRelation;

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

    bool render_player_input_error(
        std::string& error, const bool close_requested, const Translations& translations) {
        const auto title =
            std::string(text(translations, "Input Settings Error")) + "###Input Settings Error";
        const bool already_open = ImGui::IsPopupOpen(title.c_str());
        if(error.empty() && !already_open)
            return false;
        if(!error.empty() && !already_open)
            ImGui::OpenPopup(title.c_str());

        const auto* viewport = ImGui::GetMainViewport();
        const ImVec2 maximum(
            std::max(1.f, viewport->WorkSize.x - 16), std::max(1.f, viewport->WorkSize.y - 16));
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(320.f, maximum.x), std::min(160.f, maximum.y)), maximum);
        ImGui::SetNextWindowSize(
            ImVec2(std::min(470.f, maximum.x), std::min(220.f, maximum.y)), ImGuiCond_Appearing);
        const auto* previous = ImGui::FindWindowByName(title.c_str());
        if(previous && already_open) {
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
