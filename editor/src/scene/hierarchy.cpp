#include "scene/hierarchy.h"
#include "scene/selection.h"
#include "scene/command_history.h"
#include "scene/scene_commands.h"
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

    HierarchyPanel::HierarchyPanel(SelectionService& selection, const CommandHistory& history,
        const EditorState& state, const SceneCommands::EntityClipboard& clipboard)
        : EditorPanel("Hierarchy"), m_selection(selection), m_history(history), m_state(state),
          m_clipboard(clipboard) {}

    bool HierarchyPanel::can_edit_scene() const {
        return m_state.mode == EditorMode::Edit
               && m_history.get_scene() == &m_selection.get_scene();
    }

    void HierarchyPanel::reset_for_scene_change() {
        m_request.reset();
        m_rename_request.reset();
        m_renaming_entity = {};
        m_open_rename = false;
        m_expand_entity = {};
    }

    std::optional<HierarchyPanel::Request> HierarchyPanel::take_request() {
        auto request = std::exchange(m_request, std::nullopt);
        if(!can_edit_scene() || (request && request->generation != m_history.generation()))
            return std::nullopt;
        return request;
    }

    std::optional<HierarchyPanel::RenameRequest> HierarchyPanel::take_rename_request() {
        auto request = std::exchange(m_rename_request, std::nullopt);
        if(!can_edit_scene() || (request && request->generation != m_history.generation()))
            return std::nullopt;
        return request;
    }

    void HierarchyPanel::accept_reparent_drop(const Comet::Entity parent) {
        if(!can_edit_scene() || !ImGui::BeginDragDropTarget()) {
            return;
        }

        if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ENTITY_PAYLOAD_TYPE);
            payload && payload->DataSize == sizeof(EntityPayload)) {
            const auto& source = *static_cast<const EntityPayload*>(payload->Data);
            if(source.generation == m_history.generation()
                && m_selection.get_scene().find_entity(source.entity))
                m_request = Request{Request::Type::Reparent, source.entity,
                    parent ? parent.get_uuid() : Comet::EntityUuid{}, source.generation};
        }
        ImGui::EndDragDropTarget();
    }

    void HierarchyPanel::render_entity_node(const Comet::Entity entity) {
        const std::vector<Comet::Entity> children = m_selection.get_scene().get_children(entity);
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
        if(ImGui::MenuItem(Ui::label(entity ? "Create Child" : "Create Entity").c_str())) {
            const auto parent = entity ? entity.get_uuid() : Comet::EntityUuid{};
            m_request = Request{Request::Type::Create, {}, parent, m_history.generation()};
            m_expand_entity = parent;
        }
        if(ImGui::MenuItem(Ui::label("Paste").c_str(), nullptr, false, m_clipboard.has_content())) {
            const auto parent = entity ? entity.get_uuid() : Comet::EntityUuid{};
            m_request = Request{Request::Type::Paste, {}, parent, m_history.generation()};
            m_expand_entity = parent;
        }
        if(entity) {
            ImGui::Separator();
            if(ImGui::MenuItem(Ui::label("Rename").c_str())) {
                m_renaming_entity = entity.get_uuid();
                m_rename_generation = m_history.generation();
                m_rename_name = entity.get_component<Comet::NameComponent>().name;
                m_open_rename = true;
            }
            if(ImGui::MenuItem(Ui::label("Duplicate").c_str()))
                m_request = Request{
                    Request::Type::Duplicate, entity.get_uuid(), {}, m_history.generation()};
            if(ImGui::MenuItem(Ui::label("Delete").c_str()))
                m_request =
                    Request{Request::Type::Delete, entity.get_uuid(), {}, m_history.generation()};
        }
        ImGui::EndDisabled();
    }

    void HierarchyPanel::render_rename_dialog() {
        constexpr const char* title = "Rename Entity";
        const bool opening = std::exchange(m_open_rename, false);
        if(opening)
            ImGui::OpenPopup(title);
        if(!ImGui::BeginPopupModal(
               Ui::label(title).c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        const bool valid = can_edit_scene() && m_rename_generation == m_history.generation()
                           && m_selection.get_scene().find_entity(m_renaming_entity);
        if(!valid) {
            ImGui::CloseCurrentPopup();
            m_renaming_entity = {};
            ImGui::EndPopup();
            return;
        }

        if(opening)
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(360.0f);
        const bool submitted = ImGui::InputText(
            Ui::label("Name").c_str(), m_rename_name.data(), m_rename_name.capacity() + 1,
            ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue
                | ImGuiInputTextFlags_AutoSelectAll,
            [](ImGuiInputTextCallbackData* data) {
                auto& name = *static_cast<std::string*>(data->UserData);
                name.resize(static_cast<std::size_t>(data->BufTextLen));
                data->Buf = name.data();
                return 0;
            },
            &m_rename_name);
        if(ImGui::Button(Ui::label("Rename").c_str(), ImVec2(100.0f, 0.0f)) || submitted) {
            if(!m_rename_name.empty()) {
                m_rename_request =
                    RenameRequest{m_renaming_entity, m_rename_name, m_rename_generation};
                ImGui::CloseCurrentPopup();
                m_renaming_entity = {};
            }
        }
        ImGui::SameLine();
        if(ImGui::Button(Ui::label("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            m_renaming_entity = {};
        }
        ImGui::EndPopup();
    }

    void HierarchyPanel::render() {
        if(!m_user_visible)
            return;

        if(!ImGui::Begin(window_label().c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
                                   | ImGuiTreeNodeFlags_SpanAvailWidth;
        if(m_selection.get_selected_scene())
            flags |= ImGuiTreeNodeFlags_Selected;
        const bool scene_open = ImGui::TreeNodeEx(Ui::label("Scene").c_str(), flags);
        if(ImGui::IsItemClicked())
            m_selection.select_scene();
        if(ImGui::BeginPopupContextItem("Scene actions")) {
            render_context_menu({});
            ImGui::EndPopup();
        }
        accept_reparent_drop({});
        if(scene_open) {
            for(const Comet::Entity root : m_selection.get_scene().get_root_entities()) {
                render_entity_node(root);
            }
            ImGui::TreePop();
        }

        if(ImGui::BeginPopupContextWindow("Hierarchy actions",
               ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            render_context_menu({});
            ImGui::EndPopup();
        }

        render_rename_dialog();

        ImGui::End();
    }
}
