#include "hierarchy.h"
#include "selection.h"

#include <algorithm>
#include <imgui.h>
#include <string>
#include <vector>

namespace CometEditor {
    HierarchyPanel::HierarchyPanel(Comet::Scene& scene, SelectionService& selection)
        : EditorPanel("Hierarchy"), m_scene(scene), m_selection(selection) {}

    void HierarchyPanel::render() {
        if(!m_user_visible) return;

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }

        Comet::Entity selected_entity = m_selection.get_selected_entity();

        if(ImGui::Button("+")) {
            selected_entity = m_scene.create_entity();
            m_selection.select_entity(selected_entity.get_id());
        }
        if(ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Create entity");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!selected_entity);
        if(ImGui::Button("-")) {
            m_scene.destroy_entity(selected_entity);
            m_selection.clear();
        }
        if(ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Delete selected entity");
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextUnformatted("Scene");

        ImGui::Separator();

        std::vector<Comet::Entity> entities = m_scene.get_entities();
        std::sort(entities.begin(), entities.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.get_id() < rhs.get_id();
        });
        for(const Comet::Entity entity: entities) {
            const auto& name = entity.get_component<Comet::NameComponent>().name;
            const std::string display_name = name.empty() ? "<Unnamed Entity>" : name;
            const std::string label = display_name + "##entity_" + std::to_string(entity.get_id());

            if(ImGui::Selectable(
                label.c_str(), m_selection.is_selected(entity.get_id()))) {
                m_selection.select_entity(entity.get_id());
            }
        }

        ImGui::End();
    }
}
