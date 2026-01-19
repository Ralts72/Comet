#include "viewport_panel.h"
#include <imgui.h>

namespace CometEditor {

    ViewportPanel::ViewportPanel()
        : EditorPanel("Viewport") {
    }

    void ViewportPanel::render() {
        if (!m_visible) return;

        ImGui::Begin(m_name.c_str(), &m_visible);

        // 视口工具栏
        ImGui::Text("Viewport");
        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
        if (ImGui::Button("2D")) {
            m_viewport_mode_2d = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("3D")) {
            m_viewport_mode_2d = false;
        }

        ImGui::Separator();

        // 视口区域（这里显示场景渲染的内容）
        ImVec2 viewport_size = ImGui::GetContentRegionAvail();
        if (viewport_size.x < 50.0f) viewport_size.x = 50.0f;
        if (viewport_size.y < 50.0f) viewport_size.y = 50.0f;

        // 显示场景渲染内容
        if (m_texture_id != ImTextureID_Invalid && m_texture_width > 0 && m_texture_height > 0) {
            // 计算显示尺寸，保持宽高比
            float aspect_ratio = static_cast<float>(m_texture_width) / static_cast<float>(m_texture_height);
            ImVec2 display_size = viewport_size;
            if (display_size.x / aspect_ratio < display_size.y) {
                display_size.y = display_size.x / aspect_ratio;
            } else {
                display_size.x = display_size.y * aspect_ratio;
            }

            // 居中显示
            ImVec2 pos = ImGui::GetCursorPos();
            ImVec2 center_offset((viewport_size.x - display_size.x) * 0.5f, (viewport_size.y - display_size.y) * 0.5f);
            ImGui::SetCursorPos(ImVec2(pos.x + center_offset.x, pos.y + center_offset.y));

            ImGui::Image(m_texture_id, display_size);
        } else {
            // 如果没有纹理，显示占位符
            ImGui::InvisibleButton("Viewport", viewport_size);
        }

        // 视口信息
        ImGui::Text("Viewport: %.0fx%.0f", viewport_size.x, viewport_size.y);
        if (m_texture_width > 0 && m_texture_height > 0) {
            ImGui::Text("Texture: %ux%u", m_texture_width, m_texture_height);
        }

        ImGui::End();
    }

    void ViewportPanel::set_texture_id(ImTextureID texture_id, uint32_t width, uint32_t height) {
        m_texture_id = texture_id;
        m_texture_width = width;
        m_texture_height = height;
    }
}

