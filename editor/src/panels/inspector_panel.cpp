#include "inspector_panel.h"
#include <imgui.h>

namespace CometEditor {

    InspectorPanel::InspectorPanel() 
        : EditorPanel("Inspector") {
    }

    void InspectorPanel::render() {
        if (!m_visible) return;
        
        ImGui::Begin(m_name.c_str(), &m_visible);
        
        if (m_selected_object.empty()) {
            ImGui::Text("No object selected");
            ImGui::End();
            return;
        }
        
        ImGui::Text("Selected: %s", m_selected_object.c_str());
        ImGui::Separator();
        
        // Transform 组件
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Position");
            ImGui::DragFloat3("##Position", m_position, 0.1f);
            
            ImGui::Text("Rotation");
            ImGui::DragFloat3("##Rotation", m_rotation, 1.0f);
            
            ImGui::Text("Scale");
            ImGui::DragFloat3("##Scale", m_scale, 0.1f);
        }
        
        // Mesh Renderer 组件
        if (ImGui::CollapsingHeader("Mesh Renderer")) {
            ImGui::Text("Mesh: Cube");
            ImGui::Text("Material: Default");
        }
        
        // 添加组件按钮
        ImGui::Separator();
        if (ImGui::Button("Add Component")) {
            ImGui::OpenPopup("AddComponentPopup");
        }
        
        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (ImGui::MenuItem("Mesh Renderer")) {}
            if (ImGui::MenuItem("Light")) {}
            if (ImGui::MenuItem("Camera")) {}
            if (ImGui::MenuItem("Rigidbody")) {}
            ImGui::EndPopup();
        }
        
        ImGui::End();
    }

}

