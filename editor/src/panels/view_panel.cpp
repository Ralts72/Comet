#include "view_panel.h"
#include <imgui.h>
#include <algorithm>

namespace CometEditor {

    ViewPanel::ViewPanel(ViewType view_type)
        : EditorPanel(view_type == ViewType::SceneView ? "SceneView" : "GameView"),
          m_view_type(view_type) {
    }

    void ViewPanel::render() {
        // 重置实际可见性
        m_actually_visible = false;

        if (!m_user_visible) {
            return;
        }

        if (!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }

        // 检查窗口是否被折叠
        if (ImGui::IsWindowCollapsed()) {
            ImGui::End();
            return;
        }

        // 窗口可见且未被折叠，更新实际可见性
        m_actually_visible = true;

        // 工具栏
        float button_width = 50.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - button_width * 2 - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button("2D", ImVec2(button_width, 0))) {
            m_2d_mode = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("3D", ImVec2(button_width, 0))) {
            m_2d_mode = false;
        }

        ImGui::Separator();

        // 渲染视图内容
        render_view_content();

        ImGui::End();
    }

    void ViewPanel::render_view_content() {
        const char* view_title = (m_view_type == ViewType::SceneView) ? "Scene View" : "Game View";

        // 获取视口区域
        ImVec2 viewport_size = ImGui::GetContentRegionAvail();
        viewport_size.x = std::max(viewport_size.x, 50.0f);
        viewport_size.y = std::max(viewport_size.y, 50.0f);

        // 显示场景渲染内容
        if (m_texture_id != ImTextureID_Invalid && m_texture_width > 0 && m_texture_height > 0) {
            // 计算显示尺寸，保持宽高比
            const float aspect_ratio = static_cast<float>(m_texture_width) / static_cast<float>(m_texture_height);
            ImVec2 display_size = viewport_size;
            if (display_size.x / aspect_ratio < display_size.y) {
                display_size.y = display_size.x / aspect_ratio;
            } else {
                display_size.x = display_size.y * aspect_ratio;
            }

            // 居中显示
            const ImVec2 pos = ImGui::GetCursorPos();
            const ImVec2 center_offset((viewport_size.x - display_size.x) * 0.5f, (viewport_size.y - display_size.y) * 0.5f);
            ImGui::SetCursorPos(ImVec2(pos.x + center_offset.x, pos.y + center_offset.y));
            ImGui::Image(m_texture_id, display_size);
        } else {
            // 如果没有纹理，显示占位符
            ImGui::Text("Rendering...");
            ImGui::InvisibleButton("View", viewport_size);
        }

        // 显示视口信息
        ImGui::Text("%s: %.0fx%.0f", view_title, viewport_size.x, viewport_size.y);
        if (m_texture_width > 0 && m_texture_height > 0) {
            ImGui::Text("Texture: %ux%u", m_texture_width, m_texture_height);
        }
    }

    void ViewPanel::set_texture_id(ImTextureID texture_id, uint32_t width, uint32_t height) {
        m_texture_id = texture_id;
        m_texture_width = width;
        m_texture_height = height;
    }

    void ViewPanel::clear_texture() {
        m_texture_id = ImTextureID_Invalid;
        m_texture_width = 0;
        m_texture_height = 0;
    }
}


