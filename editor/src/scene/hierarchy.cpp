#include "scene/hierarchy.h"
#include "scene/selection.h"
#include "scene/command_history.h"
#include "editor_state.h"

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

    HierarchyPanel::HierarchyPanel(Comet::Scene& scene, SelectionService& selection,
        const CommandHistory& history, const EditorState& state)
        : EditorPanel("Hierarchy"), m_scene(&scene), m_selection(selection),
          m_history(history), m_state(state) {}

    bool HierarchyPanel::can_edit_scene() const {
        return m_state.mode == EditorMode::Edit && m_history.get_scene() == m_scene;
    }

    void HierarchyPanel::set_scene(Comet::Scene& scene) {
        m_scene = &scene;
        m_request.reset();
        m_expand_entity = {};
    }

    std::optional<HierarchyPanel::Request> HierarchyPanel::take_request() {
        auto request = std::exchange(m_request, std::nullopt);
        if(!can_edit_scene()
            || (request && request->generation != m_history.generation()))
            return std::nullopt;
        return request;
    }

    void HierarchyPanel::accept_reparent_drop(const Comet::Entity parent) {
        if(!can_edit_scene() || !ImGui::BeginDragDropTarget()) {
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
        if(m_expand_entity == entity.get_uuid()) {
            ImGui::SetNextItemOpen(true);
            m_expand_entity = {};
        }
        const bool open = ImGui::TreeNodeEx(node_id, flags, "%s", display_name.c_str());
        if(ImGui::IsItemClicked()) {
            m_selection.select_entity(entity.get_id());
        }
        if(ImGui::BeginPopupContextItem()) {
            render_context_menu(entity);
            ImGui::EndPopup();
        }

        if(can_edit_scene() && ImGui::BeginDragDropSource()) {
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

    void HierarchyPanel::render_context_menu(const Comet::Entity entity) {
        ImGui::BeginDisabled(!can_edit_scene());
        if(ImGui::MenuItem(entity ? "Create Child" : "Create Entity")) {
            const auto parent = entity ? entity.get_uuid() : Comet::EntityUuid{};
            m_request =
                Request{Request::Type::Create, {}, parent, m_history.generation()};
            m_expand_entity = parent;
        }
        if(entity) {
            ImGui::Separator();
            if(ImGui::MenuItem("Duplicate"))
                m_request = Request{Request::Type::Duplicate, entity.get_uuid(), {},
                    m_history.generation()};
            if(ImGui::MenuItem("Delete"))
                m_request = Request{
                    Request::Type::Delete, entity.get_uuid(), {}, m_history.generation()};
        }
        ImGui::EndDisabled();
    }

    void HierarchyPanel::render() {
        if(!m_user_visible)
            return;

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }

        const bool scene_open = ImGui::TreeNodeEx(
            "Scene", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
                         | ImGuiTreeNodeFlags_SpanAvailWidth);
        if(ImGui::BeginPopupContextItem("Scene actions")) {
            render_context_menu({});
            ImGui::EndPopup();
        }
        accept_reparent_drop({});
        if(scene_open) {
            for(const Comet::Entity root : m_scene->get_root_entities()) {
                render_entity_node(root);
            }
            ImGui::TreePop();
        }

        if(ImGui::BeginPopupContextWindow("Hierarchy actions",
               ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            render_context_menu({});
            ImGui::EndPopup();
        }

        ImGui::End();
    }
}
