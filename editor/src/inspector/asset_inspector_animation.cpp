#include "inspector/asset_inspector.h"
#include "inspector/animation_preview.h"
#include <imgui.h>

namespace CometEditor {
    bool AssetInspector::finish_controller_edit() {
        if(!m_controller_data || !m_controller_saved || *m_controller_data == *m_controller_saved)
            return true;
        if(auto valid = m_controller_data->validate(); !valid) {
            m_asset_error = "控制器尚未保存：" + valid.error();
            m_selection_request = m_selected_asset;
            return false;
        }
        m_asset_edit = AssetEdit{
            m_loaded_target, AnimationControllerEdit{*m_controller_saved, *m_controller_data}};
        return true;
    }

    bool AssetInspector::undo_controller(bool redo) {
        if(!m_controller_data)
            return false;
        return redo ? m_controller_editor.redo(*m_controller_data)
                    : m_controller_editor.undo(*m_controller_data);
    }

    void AssetInspector::render_controller_window(AnimationPreview* preview) {
        if(!m_controller_data || !m_asset_database.is_current(m_loaded_target))
            return;
        const auto action = m_controller_editor.render_window(*m_controller_data,
            *m_controller_data != *m_controller_saved, preview,
            m_asset_database.find(m_loaded_target.handle)->path.generic_string());
        if(action == AnimationControllerEditor::Action::Save)
            static_cast<void>(finish_controller_edit());
        else if(action == AnimationControllerEditor::Action::Discard) {
            m_controller_data = *m_controller_saved;
            m_controller_editor.reset(*m_controller_data);
        }
    }

    void AssetInspector::render_animation_controller() {
        auto& data = *m_controller_data;
        if(ImGui::Button("打开动画控制器###Open Animation Controller"))
            m_controller_editor.open();
        m_controller_editor.render_details(data);
        const bool dirty = data != *m_controller_saved;
        ImGui::Separator();
        ImGui::TextDisabled("%s", dirty ? "有未保存修改" : "已保存");
        ImGui::BeginDisabled(!data.validate() || !dirty);
        if(ImGui::Button("保存控制器###Save Controller"))
            static_cast<void>(finish_controller_edit());
        ImGui::EndDisabled();
        if(m_next_asset && dirty)
            ImGui::OpenPopup("未保存的动画控制器");
        if(ImGui::BeginPopupModal(
               "未保存的动画控制器", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("切换前保存当前控制器？");
            ImGui::BeginDisabled(!data.validate());
            if(ImGui::Button("保存并切换")) {
                m_asset_edit =
                    AssetEdit{m_loaded_target, AnimationControllerEdit{*m_controller_saved, data}};
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if(ImGui::Button("放弃并切换")) {
                const auto next = *m_next_asset;
                data = *m_controller_saved;
                select(next);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button("继续编辑")) {
                m_selection_request = m_selected_asset;
                m_next_asset.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}
