#include "hierarchy_panel.h"
#include <imgui.h>

namespace CometEditor {

    HierarchyPanel::HierarchyPanel()
        : EditorPanel("Hierarchy") {
    }

    void HierarchyPanel::render() {
        if (!m_user_visible) return;

        ImGui::Begin(m_name.c_str(), &m_user_visible);
        
        // 工具栏
        if (ImGui::Button("+")) {
            // TODO: 创建新对象
        }
        ImGui::SameLine();
        if (ImGui::Button("-")) {
            // TODO: 删除选中对象
        }
        ImGui::SameLine();
        ImGui::Text("Scene");
        
        ImGui::Separator();
        
        // 场景对象树（示例数据）
        if (ImGui::TreeNodeEx("Main Camera", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Leaf)) {
            if (ImGui::IsItemClicked()) {
                m_selected_object = "Main Camera";
                if (m_selection_callback) {
                    m_selection_callback(m_selected_object);
                }
            }
            ImGui::TreePop();
        }
        
        if (ImGui::TreeNodeEx("Directional Light", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Leaf)) {
            if (ImGui::IsItemClicked()) {
                m_selected_object = "Directional Light";
                if (m_selection_callback) {
                    m_selection_callback(m_selected_object);
                }
            }
            ImGui::TreePop();
        }
        
        if (ImGui::TreeNodeEx("Cube", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::IsItemClicked()) {
                m_selected_object = "Cube";
                if (m_selection_callback) {
                    m_selection_callback(m_selected_object);
                }
            }
            if (ImGui::TreeNodeEx("Mesh", ImGuiTreeNodeFlags_Leaf)) {
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_Leaf)) {
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        
        ImGui::End();
    }

}

