#include "hierarchy.h"
#include "selection.h"
#include "command_history.h"

#include <cstdint>
#include <imgui.h>
#include <string>
#include <vector>
#include <utility>

namespace CometEditor {
    namespace {
        constexpr const char* ENTITY_PAYLOAD_TYPE = "COMET_ENTITY_UUID";
        struct EntityPayload {
            Comet::EntityUuid entity;
            std::uint64_t generation;
        };
    }

    HierarchyPanel::HierarchyPanel(
        Comet::Scene& scene, SelectionService& selection, const CommandHistory& history)
        : EditorPanel("Hierarchy"), m_scene(&scene), m_selection(selection),
          m_history(history) {}

    void HierarchyPanel::set_scene(Comet::Scene& scene) {
        m_scene = &scene;
        m_request.reset();
    }

    std::optional<HierarchyPanel::Request> HierarchyPanel::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    void HierarchyPanel::accept_reparent_drop(const Comet::Entity parent) {
        if(m_history.get_scene() != m_scene || !ImGui::BeginDragDropTarget()) {
            return;
        }

        if(const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(ENTITY_PAYLOAD_TYPE);
            payload && payload->DataSize == sizeof(EntityPayload)) {
            const auto& source = *static_cast<const EntityPayload*>(payload->Data);
            if(source.generation == m_history.generation()
                && m_scene->find_entity(source.entity))
                m_request = Request{Request::Type::Reparent, source.entity,
                    parent ? parent.get_uuid() : Comet::EntityUuid{}, source.generation};
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

        if(m_history.get_scene() == m_scene && ImGui::BeginDragDropSource()) {
            const EntityPayload payload{entity.get_uuid(), m_history.generation()};
            ImGui::SetDragDropPayload(ENTITY_PAYLOAD_TYPE, &payload, sizeof(payload));
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

        ImGui::BeginDisabled(m_history.get_scene() != m_scene);
        if(ImGui::Button("+"))
            m_request = Request{Request::Type::Create, {}, {}, m_history.generation()};
        if(ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Create entity");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!selected_entity);
        if(ImGui::Button("-")) {
            m_request = Request{Request::Type::Delete, selected_entity.get_uuid(), {},
                m_history.generation()};
        }
        if(ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Delete selected entity");
        }
        ImGui::EndDisabled();
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
