#include "ui/dialogs.h"
#include "ui/language.h"

#include <imgui.h>

namespace CometEditor {
    std::optional<bool> draw_material_template_dialog(const bool pending,
        const std::string& template_name, const std::span<const std::string> discarded_properties) {
        constexpr const char* title = "Change Material Template";
        if(pending)
            ImGui::OpenPopup(title);
        if(!ImGui::BeginPopupModal(
               Ui::label(title).c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return std::nullopt;
        std::optional<bool> decision;
        if(!pending) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text(Ui::text("Switch to %s?"), template_name.c_str());
            if(!discarded_properties.empty()) {
                ImGui::TextUnformatted(Ui::text("Incompatible properties will be discarded:"));
                for(const auto& name : discarded_properties)
                    ImGui::BulletText("%s", name.c_str());
            }
            if(ImGui::Button(Ui::label("Switch").c_str()))
                decision = true;
            ImGui::SameLine();
            if(ImGui::Button(Ui::label("Cancel").c_str()))
                decision = false;
            if(decision)
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return decision;
    }

    std::optional<SceneDocument::Decision> draw_unsaved_scene_dialog(
        const bool needs_confirmation) {
        if(needs_confirmation)
            ImGui::OpenPopup("Unsaved Scene");
        if(!ImGui::BeginPopupModal(
               Ui::label("Unsaved Scene").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return std::nullopt;
        std::optional<SceneDocument::Decision> decision;
        ImGui::TextUnformatted(Ui::text("Save changes before continuing?"));
        if(ImGui::Button(Ui::label("Save").c_str()))
            decision = SceneDocument::Decision::Save;
        ImGui::SameLine();
        if(ImGui::Button(Ui::label("Discard").c_str()))
            decision = SceneDocument::Decision::Discard;
        ImGui::SameLine();
        if(ImGui::Button(Ui::label("Cancel").c_str()))
            decision = SceneDocument::Decision::Cancel;
        if(decision)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return decision;
    }
}
