#include "view.h"
#include <imgui.h>
#include <algorithm>

namespace CometEditor {
    namespace {
        constexpr std::uint32_t RESIZE_STABLE_FRAME_COUNT = 2;
    }

    ViewPanel::ViewPanel(const EditorState& state)
        : EditorPanel("Viewport"), m_state(state) {}

    void ViewPanel::render() {
        m_actually_visible = false;

        if(!m_user_visible) {
            m_viewport_size = Comet::Math::Vec2u(0);
            m_viewport_size_stable_frames = 0;
            return;
        }

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            m_viewport_size = Comet::Math::Vec2u(0);
            m_viewport_size_stable_frames = 0;
            ImGui::End();
            return;
        }

        if(ImGui::IsWindowCollapsed()) {
            m_viewport_size = Comet::Math::Vec2u(0);
            m_viewport_size_stable_frames = 0;
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
        ImVec2 viewport_size = ImGui::GetContentRegionAvail();
        viewport_size.x = std::max(viewport_size.x, 1.0f);
        viewport_size.y = std::max(viewport_size.y, 1.0f);
        const Comet::Math::Vec2u new_viewport_size(
            static_cast<std::uint32_t>(viewport_size.x),
            static_cast<std::uint32_t>(viewport_size.y));
        if(new_viewport_size != m_viewport_size) {
            m_viewport_size = new_viewport_size;
            m_viewport_size_stable_frames = 0;
        } else if(m_viewport_size_stable_frames
                  < RESIZE_STABLE_FRAME_COUNT) {
            ++m_viewport_size_stable_frames;
        }

        if(m_texture_id != ImTextureID_Invalid && m_texture_width > 0 && m_texture_height > 0) {
            const float aspect_ratio = static_cast<float>(m_texture_width) / static_cast<float>(m_texture_height);
            ImVec2 display_size = viewport_size;
            if(display_size.x / aspect_ratio < display_size.y) {
                display_size.y = display_size.x / aspect_ratio;
            } else {
                display_size.x = display_size.y * aspect_ratio;
            }

            const ImVec2 pos = ImGui::GetCursorPos();
            const ImVec2 center_offset((viewport_size.x - display_size.x) * 0.5f,
                (viewport_size.y - display_size.y) * 0.5f);
            ImGui::SetCursorPos(ImVec2(pos.x + center_offset.x, pos.y + center_offset.y));
            ImGui::Image(m_texture_id, display_size);
        } else {
            ImGui::InvisibleButton("View", viewport_size);
        }
    }

    void ViewPanel::set_texture_id(const ImTextureID texture_id, const std::uint32_t width, const std::uint32_t height) {
        m_texture_id = texture_id;
        m_texture_width = width;
        m_texture_height = height;
    }

    void ViewPanel::clear_texture() {
        m_texture_id = ImTextureID_Invalid;
        m_texture_width = 0;
        m_texture_height = 0;
    }

    std::optional<Comet::Math::Vec2u> ViewPanel::take_resize_request() {
        if(m_viewport_size_stable_frames < RESIZE_STABLE_FRAME_COUNT
           || m_viewport_size == Comet::Math::Vec2u(0)
           || m_viewport_size == m_last_requested_size) {
            return std::nullopt;
        }

        m_last_requested_size = m_viewport_size;
        return m_viewport_size;
    }
}
