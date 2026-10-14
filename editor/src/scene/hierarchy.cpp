#include "scene/hierarchy.h"
#include "scene/entity_reference.h"
#include "scene/selection.h"
#include "scene/command_history.h"
#include "scene/scene_commands.h"
#include "editor_state.h"
#include "ui/widgets.h"

#include <cstdint>
#include <imgui.h>
#include <string>
#include <vector>
#include <utility>

namespace CometEditor {
    HierarchyPanel::HierarchyPanel(SelectionService& selection, const CommandHistory& history,
        const EditorState& state, const SceneCommands::EntityClipboard& clipboard)
        : EditorPanel("层级###Hierarchy"), m_selection(selection), m_history(history),
          m_state(state), m_clipboard(clipboard) {}

    bool HierarchyPanel::can_edit_scene() const {
        return m_state.mode == EditorMode::Edit
               && m_history.get_scene() == &m_selection.get_scene();
    }

    void HierarchyPanel::reset_for_scene_change() {
        m_request.reset();
        m_prefab_request.reset();
        m_rename_request.reset();
        m_renaming_entity = {};
        m_open_rename = false;
        m_expand_entity = {};
        m_visible_entities.clear();
        m_selection_anchor = {};
        m_clicked_entity = {};
        m_pressed_entity = {};
        m_placement_request.reset();
        m_open_placement = false;
    }

    std::optional<HierarchyPanel::Request> HierarchyPanel::take_request() {
        auto request = std::exchange(m_request, std::nullopt);
        if(!can_edit_scene() || (request && request->generation != m_history.generation()))
            return std::nullopt;
        return request;
    }

    std::optional<HierarchyPanel::PrefabRequest> HierarchyPanel::take_prefab_request() {
        auto request = std::exchange(m_prefab_request, std::nullopt);
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
        if(!can_edit_scene()) {
            return;
        }

        if(const auto source = accept_entity_drop(m_selection.get_scene(), m_history.generation()))
            m_request = Request{Request::Type::Reparent, *source,
                parent ? parent.get_uuid() : Comet::EntityUuid{}, m_history.generation()};
    }

    void HierarchyPanel::render_entity_node(const Comet::Entity entity) {
        m_visible_entities.push_back(entity.get_uuid());
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
        // 树节点按住修饰键时不会激活；独立记录按下项，松开才选择，起拖保留原目标。
        if(ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_pressed_entity = entity.get_uuid();
        if(m_pressed_entity == entity.get_uuid() && ImGui::IsItemHovered()
            && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGui::GetDragDropPayload()) {
            m_clicked_entity = entity.get_uuid();
        }
        if(ImGui::BeginPopupContextItem()) {
            render_context_menu(entity);
            ImGui::EndPopup();
        }

        if(can_edit_scene() && ImGui::BeginDragDropSource()) {
            const EntityDragPayload payload{entity.get_uuid(), m_history.generation()};
            ImGui::SetDragDropPayload(EntityDragPayload::TYPE, &payload, sizeof(payload));
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
        if(ImGui::MenuItem(entity ? "创建子实体###Create Child" : "创建实体###Create Entity")) {
            const auto parent = entity ? entity.get_uuid() : Comet::EntityUuid{};
            m_request = Request{Request::Type::Create, {}, parent, m_history.generation()};
            m_expand_entity = parent;
        }
        if(ImGui::MenuItem("粘贴###Paste", nullptr, false, m_clipboard.has_content())) {
            const auto parent = entity ? entity.get_uuid() : Comet::EntityUuid{};
            m_request = Request{Request::Type::Paste, {}, parent, m_history.generation()};
            m_expand_entity = parent;
        }
        if(entity) {
            ImGui::Separator();
            if(ImGui::MenuItem("重命名###Rename")) {
                m_renaming_entity = entity.get_uuid();
                m_rename_generation = m_history.generation();
                m_rename_name = entity.get_component<Comet::NameComponent>().name;
                m_open_rename = true;
            }
            const auto targets = m_selection.is_selected(entity.get_id())
                                     ? m_selection.get_selected_entities()
                                     : std::vector{entity.get_uuid()};
            if(ImGui::MenuItem(
                   targets.size() > 1 ? "复制选中实体###Duplicate" : "复制###Duplicate"))
                m_request = Request{Request::Type::Duplicate, entity.get_uuid(), {},
                    m_history.generation(), targets};
            if(ImGui::MenuItem("批量放置...###Place Copies")) {
                m_placement_request = Request{Request::Type::Duplicate, entity.get_uuid(), {},
                    m_history.generation(), targets, {.copies = 3, .world_step = {2, 0, 0}}};
                m_open_placement = true;
            }
            if(ImGui::MenuItem("保存为 Prefab...###Save Prefab"))
                m_prefab_request = PrefabRequest{entity.get_uuid(), m_history.generation()};
            if(entity.has_component<Comet::PrefabInstanceComponent>()) {
                const bool model =
                    entity.get_component<Comet::PrefabInstanceComponent>().source_type
                    == Comet::AssetType::Model;
                if(ImGui::MenuItem(model ? "从模型更新（保留覆盖）###Update Prefab"
                                         : "从 Prefab 更新（保留覆盖）###Update Prefab"))
                    m_request = Request{
                        Request::Type::UpdatePrefab, entity.get_uuid(), {}, m_history.generation()};
                if(!model && ImGui::MenuItem("写回 Prefab 源###Apply Prefab"))
                    m_request = Request{
                        Request::Type::ApplyPrefab, entity.get_uuid(), {}, m_history.generation()};
                if(ImGui::MenuItem(model ? "恢复模型源内容###Revert Prefab"
                                         : "恢复 Prefab 源内容###Revert Prefab"))
                    m_request = Request{
                        Request::Type::RevertPrefab, entity.get_uuid(), {}, m_history.generation()};
                if(ImGui::MenuItem(
                       model ? "解除模型关联###Detach Prefab" : "解除 Prefab 关联###Detach Prefab"))
                    m_request = Request{
                        Request::Type::DetachPrefab, entity.get_uuid(), {}, m_history.generation()};
            }
            if(ImGui::MenuItem(targets.size() > 1 ? "删除选中实体###Delete" : "删除###Delete"))
                m_request = Request{
                    Request::Type::Delete, entity.get_uuid(), {}, m_history.generation(), targets};
        }
        ImGui::EndDisabled();
    }

    void HierarchyPanel::render_rename_dialog() {
        constexpr const char* title = "重命名实体###Rename Entity";
        const bool opening = std::exchange(m_open_rename, false);
        if(opening)
            ImGui::OpenPopup(title);
        if(!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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
        const bool submitted = Ui::input_text("名称###Name", m_rename_name,
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if(ImGui::Button("重命名###Rename", ImVec2(100.0f, 0.0f)) || submitted) {
            if(!m_rename_name.empty()) {
                m_rename_request =
                    RenameRequest{m_renaming_entity, m_rename_name, m_rename_generation};
                ImGui::CloseCurrentPopup();
                m_renaming_entity = {};
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("取消###Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            m_renaming_entity = {};
        }
        ImGui::EndPopup();
    }

    void HierarchyPanel::render_placement_dialog() {
        constexpr const char* title = "批量放置###Place Copies";
        if(std::exchange(m_open_placement, false))
            ImGui::OpenPopup(title);
        if(!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if(!m_placement_request || !can_edit_scene()
            || m_placement_request->generation != m_history.generation()) {
            m_placement_request.reset();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        auto& options = m_placement_request->duplicate;
        ImGui::TextUnformatted("按世界坐标间距复制选中组，保留组内相对位置与引用。");
        ImGui::SetNextItemWidth(240);
        ImGui::InputInt("新增份数###Copies", &options.copies);
        ImGui::SetNextItemWidth(240);
        ImGui::InputFloat3("每份间距 XYZ###World Step", &options.world_step.x);
        const bool valid = options.copies >= 1
                           && options.copies <= SceneCommands::DuplicateOptions::MAX_COPIES
                           && Comet::Math::is_finite(options.world_step);
        if(!valid)
            ImGui::TextWrapped("份数须在 1 到 %d 之间，间距须是有限数值。",
                SceneCommands::DuplicateOptions::MAX_COPIES);
        ImGui::BeginDisabled(!valid);
        if(ImGui::Button("放置###Place", ImVec2(100, 0))) {
            m_request = std::move(m_placement_request);
            m_placement_request.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("取消###Cancel", ImVec2(100, 0))) {
            m_placement_request.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void HierarchyPanel::apply_selection() {
        const auto clicked = std::exchange(m_clicked_entity, Comet::EntityUuid{});
        if(!clicked)
            return;
        const auto& io = ImGui::GetIO();
        const bool toggle = io.KeyCtrl || io.KeySuper;
        const auto anchor =
            std::find(m_visible_entities.begin(), m_visible_entities.end(), m_selection_anchor);
        const auto target =
            std::find(m_visible_entities.begin(), m_visible_entities.end(), clicked);
        if(io.KeyShift && anchor != m_visible_entities.end()
            && target != m_visible_entities.end()) {
            auto selected =
                toggle ? m_selection.get_selected_entities() : std::vector<Comet::EntityUuid>{};
            for(auto it = anchor;; it += target >= anchor ? 1 : -1) {
                std::erase(selected, *it);
                selected.push_back(*it);
                if(it == target)
                    break;
            }
            m_selection.select_entities(selected);
            return;
        }
        const auto entity = m_selection.get_scene().find_entity(clicked);
        if(toggle)
            m_selection.toggle_entity(entity.get_id());
        else
            m_selection.select_entity(entity.get_id());
        m_selection_anchor = clicked;
    }

    void HierarchyPanel::render() {
        if(!m_user_visible)
            return;

        if(!ImGui::Begin(window_label().c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }

        m_visible_entities.clear();
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            || ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            m_pressed_entity = {};
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
                                   | ImGuiTreeNodeFlags_SpanAvailWidth;
        if(m_selection.get_selected_scene())
            flags |= ImGuiTreeNodeFlags_Selected;
        const bool scene_open = ImGui::TreeNodeEx("场景###Scene", flags);
        ImGui::SetItemTooltip("Ctrl / Command 点击多选，Shift 点击连续选择");
        if(ImGui::IsItemClicked()) {
            m_selection.select_scene();
            m_selection_anchor = {};
        }
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
        apply_selection();
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            m_pressed_entity = {};

        if(ImGui::BeginPopupContextWindow("Hierarchy actions",
               ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            render_context_menu({});
            ImGui::EndPopup();
        }

        render_rename_dialog();
        render_placement_dialog();

        ImGui::End();
    }
}
