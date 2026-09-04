#include "hierarchy.h"
#include "selection.h"

#include <cstdint>
#include <imgui.h>
#include <string>
#include <vector>

namespace CometEditor {
    namespace {
        constexpr const char* ENTITY_PAYLOAD_TYPE = "COMET_ENTITY_ID";
    }

    HierarchyPanel::HierarchyPanel(Comet::Scene& scene, SelectionService& selection)
        : EditorPanel("Hierarchy"), m_scene(&scene), m_selection(selection) {}

    void HierarchyPanel::accept_reparent_drop(const Comet::Entity parent) {
        if(!ImGui::BeginDragDropTarget()) {
            return;
        }

        if(const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(ENTITY_PAYLOAD_TYPE);
            payload && payload->DataSize == sizeof(Comet::EntityId)) {
            const auto entity_id = *static_cast<const Comet::EntityId*>(payload->Data);
            if(const Comet::Entity child = m_scene->find_entity(entity_id)) {
                if(parent) {
                    static_cast<void>(m_scene->set_parent(child, parent));
                } else {
                    static_cast<void>(m_scene->clear_parent(child));
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    void HierarchyPanel::render_entity_node(const Comet::Entity entity) {
        const std::vector<Comet::Entity> children = m_scene->get_children(entity);
        const auto& name = entity.get_component<Comet::NameComponent>().name;
        const std::string display_name = name.empty() ? "<Unnamed Entity>" : name;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                   | ImGuiTreeNodeFlags_OpenOnDoubleClick
                                   | ImGuiTreeNodeFlags_SpanAvailWidth;
        if(m_selection.is_selected(entity.get_id())) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        if(children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        const auto node_id =
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(entity.get_id()));
        const bool open = ImGui::TreeNodeEx(node_id, flags, "%s", display_name.c_str());
        if(ImGui::IsItemClicked()) {
            m_selection.select_entity(entity.get_id());
        }

        if(ImGui::BeginDragDropSource()) {
            const Comet::EntityId entity_id = entity.get_id();
            ImGui::SetDragDropPayload(ENTITY_PAYLOAD_TYPE, &entity_id, sizeof(entity_id));
            ImGui::TextUnformatted(display_name.c_str());
            ImGui::EndDragDropSource();
        }
        accept_reparent_drop(entity);

        if(open && !children.empty()) {
            for(const Comet::Entity child : children) {
                render_entity_node(child);
            }
            ImGui::TreePop();
        }
    }

    void HierarchyPanel::render() {
        if(!m_user_visible)
            return;

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }

        Comet::Entity selected_entity = m_selection.get_selected_entity();

        if(ImGui::Button("+")) {
            selected_entity = m_scene->create_entity();
            m_selection.select_entity(selected_entity.get_id());
        }
        if(ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Create entity");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!selected_entity);
        if(ImGui::Button("-")) {
            m_scene->destroy_entity(selected_entity);
            m_selection.clear();
        }
        if(ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Delete selected entity");
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        const bool scene_open = ImGui::TreeNodeEx(
            "Scene", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
                         | ImGuiTreeNodeFlags_SpanAvailWidth);
        accept_reparent_drop({});
        if(scene_open) {
            for(const Comet::Entity root : m_scene->get_root_entities()) {
                render_entity_node(root);
            }
            ImGui::TreePop();
        }

        ImGui::End();
    }
}
