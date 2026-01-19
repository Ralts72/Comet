#include "project_panel.h"
#include <imgui.h>

namespace CometEditor {

    ProjectPanel::ProjectPanel() 
        : EditorPanel("Project") {
    }

    void ProjectPanel::render() {
        if (!m_visible) return;
        
        ImGui::Begin(m_name.c_str(), &m_visible);
        
        // 工具栏
        if (ImGui::Button("Assets")) {
            m_view_mode = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Packages")) {
            m_view_mode = 1;
        }
        
        ImGui::Separator();
        
        // 资源树
        if (ImGui::TreeNodeEx("Assets", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::TreeNodeEx("Scenes", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Selectable("SampleScene.scene")) {}
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Selectable("Default.mat")) {}
                if (ImGui::Selectable("Unlit.mat")) {}
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Selectable("texture1.png")) {}
                if (ImGui::Selectable("texture2.png")) {}
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Models", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Selectable("cube.obj")) {}
                if (ImGui::Selectable("sphere.obj")) {}
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        
        ImGui::End();
    }

}

