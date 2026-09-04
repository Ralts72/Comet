#include "view.h"
#include <imgui.h>

namespace CometEditor {
    namespace {
        constexpr std::uint32_t RESIZE_STABLE_FRAME_COUNT = 2;
    }

    ViewPanel::ViewPanel(const EditorState& state)
        : EditorPanel("Viewport"), m_state(state) {}

    void ViewPanel::render() {
        m_actually_visible = false;

        if(!m_user_visible) {
            m_layout = {};
            m_observed_render_resolution = {};
            m_requested_render_size = {};
            m_render_resolution_stable_frames = 0;
            return;
        }

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            m_layout = {};
            m_observed_render_resolution = {};
            m_requested_render_size = {};
            m_render_resolution_stable_frames = 0;
            ImGui::End();
            return;
        }

        if(ImGui::IsWindowCollapsed()) {
            m_layout = {};
            m_observed_render_resolution = {};
            m_requested_render_size = {};
            m_render_resolution_stable_frames = 0;
            ImGui::End();
            return;
        }

        m_actually_visible = true;

        if(m_state.mode == EditorMode::Edit) {
            render_edit_toolbar();
        } else {
            render_play_toolbar();
        }

        render_view_content();

        ImGui::End();
    }

    void ViewPanel::render_edit_toolbar() {
        constexpr float button_width = 50.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - button_width * 2
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

    void ViewPanel::render_play_toolbar() {
        using ResolutionMode = ViewportLayout::ResolutionPolicy::Mode;
        const Comet::Math::Vec2u hd_resolution(1280, 720);
        const Comet::Math::Vec2u full_hd_resolution(1920, 1080);

        const char* resolution_label = "Free";
        if(m_play_resolution_policy.mode == ResolutionMode::Aspect16By9) {
            resolution_label = "16:9";
        } else if(m_play_resolution_policy.mode == ResolutionMode::Fixed) {
            if(m_play_resolution_policy.fixed_resolution == hd_resolution) {
                resolution_label = "1280 x 720";
            } else if(m_play_resolution_policy.fixed_resolution == full_hd_resolution) {
                resolution_label = "1920 x 1080";
            } else {
                resolution_label = "Custom";
            }
        }

        ImGui::SetNextItemWidth(150.0f);
        if(ImGui::BeginCombo("##Resolution", resolution_label)) {
            if(ImGui::Selectable(
                   "Free", m_play_resolution_policy.mode == ResolutionMode::Free)) {
                m_play_resolution_policy = {};
            }
            if(ImGui::Selectable("16:9",
                   m_play_resolution_policy.mode == ResolutionMode::Aspect16By9)) {
                m_play_resolution_policy = {.mode = ResolutionMode::Aspect16By9};
            }
            if(ImGui::Selectable("1280 x 720",
                   m_play_resolution_policy.mode == ResolutionMode::Fixed
                       && m_play_resolution_policy.fixed_resolution == hd_resolution)) {
                m_play_resolution_policy = {
                    .mode = ResolutionMode::Fixed,
                    .fixed_resolution = hd_resolution,
                };
            }
            if(ImGui::Selectable(
                   "1920 x 1080", m_play_resolution_policy.mode == ResolutionMode::Fixed
                                      && m_play_resolution_policy.fixed_resolution
                                             == full_hd_resolution)) {
                m_play_resolution_policy = {
                    .mode = ResolutionMode::Fixed,
                    .fixed_resolution = full_hd_resolution,
                };
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        const char* display_label =
            m_play_display_mode == ViewportLayout::DisplayMode::Fit ? "Fit" : "1x";
        ImGui::SetNextItemWidth(80.0f);
        if(ImGui::BeginCombo("##Display", display_label)) {
            if(ImGui::Selectable(
                   "Fit", m_play_display_mode == ViewportLayout::DisplayMode::Fit)) {
                m_play_display_mode = ViewportLayout::DisplayMode::Fit;
            }
            if(ImGui::Selectable(
                   "1x", m_play_display_mode == ViewportLayout::DisplayMode::OneToOne)) {
                m_play_display_mode = ViewportLayout::DisplayMode::OneToOne;
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();
    }

    void ViewPanel::render_view_content() {
        const ImVec2 content_size = ImGui::GetContentRegionAvail();
        const ImVec2 content_origin = ImGui::GetCursorScreenPos();
        const ImGuiViewport* window_viewport = ImGui::GetWindowViewport();
        const ImVec2 framebuffer_scale =
            window_viewport ? window_viewport->FramebufferScale : ImVec2(1.0f, 1.0f);
        m_layout = calculate_viewport_layout({
            .content_origin = {content_origin.x, content_origin.y},
            .content_size = {content_size.x, content_size.y},
            .framebuffer_scale = {framebuffer_scale.x, framebuffer_scale.y},
            .current_render_resolution = m_texture_resolution,
            .resolution_policy = m_state.mode == EditorMode::Play
                                     ? m_play_resolution_policy
                                     : ViewportLayout::ResolutionPolicy{},
            .display_mode = m_state.mode == EditorMode::Play
                                ? m_play_display_mode
                                : ViewportLayout::DisplayMode::Fit,
        });

        if(m_layout.render_resolution != m_observed_render_resolution) {
            m_observed_render_resolution = m_layout.render_resolution;
            m_render_resolution_stable_frames = 0;
        } else if(m_render_resolution_stable_frames < RESIZE_STABLE_FRAME_COUNT) {
            ++m_render_resolution_stable_frames;
            if(m_render_resolution_stable_frames == RESIZE_STABLE_FRAME_COUNT) {
                m_requested_render_size = m_observed_render_resolution;
            }
        }

        const Comet::Math::Vec2 display_size = m_layout.image_display_rect.size();
        if(display_size.x <= 0.0f || display_size.y <= 0.0f) {
            return;
        }

        ImGui::SetCursorScreenPos(
            ImVec2(m_layout.image_display_rect.min.x, m_layout.image_display_rect.min.y));
        if(m_texture_id != ImTextureID_Invalid && m_texture_resolution.x > 0
            && m_texture_resolution.y > 0) {
            ImGui::Image(m_texture_id, ImVec2(display_size.x, display_size.y));
        } else {
            ImGui::InvisibleButton("View", ImVec2(display_size.x, display_size.y));
        }
    }

    void ViewPanel::set_texture_id(const ImTextureID texture_id,
        const std::uint32_t width, const std::uint32_t height) {
        m_texture_id = texture_id;
        m_texture_resolution = Comet::Math::Vec2u(width, height);
    }

    void ViewPanel::clear_texture() {
        m_texture_id = ImTextureID_Invalid;
        m_texture_resolution = {};
    }

}
