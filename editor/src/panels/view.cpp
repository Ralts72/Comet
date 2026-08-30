#include "view.h"
#include <imgui.h>

namespace CometEditor {
    ViewPanel::ViewPanel(const EditorState& state)
        : EditorPanel("Viewport"), m_state(state) {}

    void ViewPanel::render() {
        m_actually_visible = false;
        m_layout = {};

        if(!m_user_visible) {
            return;
        }

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }

        if(ImGui::IsWindowCollapsed()) {
            ImGui::End();
            return;
        }

        m_actually_visible = true;

        if(m_state.mode == EditorMode::Edit) {
            constexpr float button_width = 50.0f;
            ImGui::SetCursorPosX(
                ImGui::GetWindowWidth() - button_width * 2
                - ImGui::GetStyle().ItemSpacing.x
                - ImGui::GetStyle().WindowPadding.x);
            if(ImGui::Button("2D", ImVec2(button_width, 0))) {
                m_2d_mode = true;
            }
            ImGui::SameLine();
            if(ImGui::Button("3D", ImVec2(button_width, 0))) {
                m_2d_mode = false;
            }
            ImGui::Separator();
        }

        render_view_content();

        ImGui::End();
    }

    void ViewPanel::render_view_content() {
        const ImVec2 content_size = ImGui::GetContentRegionAvail();
        const ImVec2 content_origin = ImGui::GetCursorScreenPos();
        const ImGuiViewport* window_viewport = ImGui::GetWindowViewport();
        const ImVec2 framebuffer_scale = window_viewport
            ? window_viewport->FramebufferScale
            : ImVec2(1.0f, 1.0f);
        m_layout = calculate_viewport_layout({
            .content_origin = {
                content_origin.x,
                content_origin.y
            },
            .content_size = {
                content_size.x,
                content_size.y
            },
            .framebuffer_scale = {
                framebuffer_scale.x,
                framebuffer_scale.y
            },
            .current_render_resolution = m_texture_resolution
        });

        const Comet::Math::Vec2 display_size =
            m_layout.image_display_rect.size();
        if(display_size.x <= 0.0f || display_size.y <= 0.0f) {
            return;
        }

        ImGui::SetCursorScreenPos(ImVec2(
            m_layout.image_display_rect.min.x,
            m_layout.image_display_rect.min.y));
        if(m_texture_id != ImTextureID_Invalid
           && m_texture_resolution.x > 0
           && m_texture_resolution.y > 0) {
            ImGui::Image(
                m_texture_id,
                ImVec2(display_size.x, display_size.y));
        } else {
            ImGui::InvisibleButton(
                "View", ImVec2(display_size.x, display_size.y));
        }
    }

    void ViewPanel::set_texture_id(const ImTextureID texture_id, const std::uint32_t width, const std::uint32_t height) {
        m_texture_id = texture_id;
        m_texture_resolution = Comet::Math::Vec2u(width, height);
    }

    void ViewPanel::clear_texture() {
        m_texture_id = ImTextureID_Invalid;
        m_texture_resolution = {};
    }
}
