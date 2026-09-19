#include "ui/dialogs.h"

#include <imgui.h>

namespace CometEditor {
    std::optional<SceneDocument::Decision> draw_unsaved_scene_dialog(
        const bool needs_confirmation) {
        if(needs_confirmation)
            ImGui::OpenPopup("Unsaved Scene");
        if(!ImGui::BeginPopupModal("Unsaved Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return std::nullopt;
        std::optional<SceneDocument::Decision> decision;
        ImGui::TextUnformatted("Save changes before continuing?");
        if(ImGui::Button("Save"))
            decision = SceneDocument::Decision::Save;
        ImGui::SameLine();
        if(ImGui::Button("Discard"))
            decision = SceneDocument::Decision::Discard;
        ImGui::SameLine();
        if(ImGui::Button("Cancel"))
            decision = SceneDocument::Decision::Cancel;
        if(decision)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return decision;
    }
}
