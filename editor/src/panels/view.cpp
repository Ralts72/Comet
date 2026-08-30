#include "view.h"
#include <imgui.h>

#include <utility>

namespace CometEditor {
    ViewPanel::ViewPanel(
        const EditorState& state,
        const std::uint32_t max_render_dimension)
        : EditorPanel("Viewport"),
          m_state(state),
          m_max_render_dimension(max_render_dimension) {}

    void ViewPanel::render() {
        m_actually_visible = false;
        m_layout = {};
        m_camera_input.reset();
        m_camera_projection_request.reset();
        m_pointer_input.reset();
        m_focus_request = false;

        if(!m_user_visible) {
            reset_view_interaction();
            return;
        }

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            reset_view_interaction();
            ImGui::End();
            return;
        }

        if(ImGui::IsWindowCollapsed()) {
            reset_view_interaction();
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
        ImGui::SetCursorPosX(
            ImGui::GetWindowWidth() - button_width * 2
            - ImGui::GetStyle().ItemSpacing.x
            - ImGui::GetStyle().WindowPadding.x);
        if(ImGui::Selectable(
               "2D",
               m_state.camera.projection
                   == Comet::CameraProjection::Orthographic,
               0,
               ImVec2(button_width, 0))) {
            m_camera_projection_request =
                Comet::CameraProjection::Orthographic;
        }
        ImGui::SameLine();
        if(ImGui::Selectable(
               "3D",
               m_state.camera.projection
                   == Comet::CameraProjection::Perspective,
               0,
               ImVec2(button_width, 0))) {
            m_camera_projection_request =
                Comet::CameraProjection::Perspective;
        }
        ImGui::Separator();
    }

    void ViewPanel::render_play_toolbar() {
        const char* resolution = "Free";
        if(m_play_resolution_policy.mode
           == ViewportResolutionMode::Aspect16By9) {
            resolution = "16:9";
        } else if(m_play_resolution_policy.mode
                  == ViewportResolutionMode::Fixed) {
            resolution = m_play_resolution_policy.fixed_resolution
                    == Comet::Math::Vec2u(1280, 720)
                ? "1280 x 720"
                : "1920 x 1080";
        }

        ImGui::SetNextItemWidth(150.0f);
        if(ImGui::BeginCombo("##Resolution", resolution)) {
            if(ImGui::Selectable(
                   "Free",
                   m_play_resolution_policy.mode
                       == ViewportResolutionMode::Free)) {
                m_play_resolution_policy = {};
            }
            if(ImGui::Selectable(
                   "16:9",
                   m_play_resolution_policy.mode
                       == ViewportResolutionMode::Aspect16By9)) {
                m_play_resolution_policy = {
                    .mode = ViewportResolutionMode::Aspect16By9
                };
            }
            if(ImGui::Selectable(
                   "1280 x 720",
                   m_play_resolution_policy.mode
                       == ViewportResolutionMode::Fixed
                       && m_play_resolution_policy.fixed_resolution
                           == Comet::Math::Vec2u(1280, 720))) {
                m_play_resolution_policy = {
                    .mode = ViewportResolutionMode::Fixed,
                    .fixed_resolution = Comet::Math::Vec2u(1280, 720)
                };
            }
            if(ImGui::Selectable(
                   "1920 x 1080",
                   m_play_resolution_policy.mode
                       == ViewportResolutionMode::Fixed
                       && m_play_resolution_policy.fixed_resolution
                           == Comet::Math::Vec2u(1920, 1080))) {
                m_play_resolution_policy = {
                    .mode = ViewportResolutionMode::Fixed,
                    .fixed_resolution = Comet::Math::Vec2u(1920, 1080)
                };
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        const char* display = m_play_display_mode
                == ViewportDisplayMode::Fit
            ? "Fit"
            : "1x";
        ImGui::SetNextItemWidth(80.0f);
        if(ImGui::BeginCombo("##Display", display)) {
            if(ImGui::Selectable(
                   "Fit",
                   m_play_display_mode == ViewportDisplayMode::Fit)) {
                m_play_display_mode = ViewportDisplayMode::Fit;
            }
            if(ImGui::Selectable(
                   "1x",
                   m_play_display_mode == ViewportDisplayMode::OneToOne)) {
                m_play_display_mode = ViewportDisplayMode::OneToOne;
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();
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
            .current_render_resolution = m_texture_resolution,
            .max_render_dimension = m_max_render_dimension,
            .resolution_policy = m_state.mode == EditorMode::Play
                ? m_play_resolution_policy
                : ViewportResolutionPolicy{},
            .display_mode = m_state.mode == EditorMode::Play
                ? m_play_display_mode
                : ViewportDisplayMode::Fit
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
        update_view_interaction();
    }

    void ViewPanel::update_view_interaction() {
        if(m_state.mode != EditorMode::Edit) {
            reset_view_interaction();
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        const Comet::Math::Vec2 mouse_position(
            io.MousePos.x, io.MousePos.y);
        const std::optional<Comet::Math::Vec2u> mapped_pixel =
            map_viewport_point_to_pixel(m_layout, mouse_position);
        const bool pointer_over_image =
            ImGui::IsItemHovered() && mapped_pixel.has_value();

        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
           && !io.WantTextInput
           && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            m_focus_request = true;
        }

        const bool left_pressed = pointer_over_image
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if(left_pressed) {
            m_left_pointer_active = true;
        }
        const bool left_released = m_left_pointer_active
            && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        if(pointer_over_image || m_left_pointer_active) {
            const auto pixel_position =
                map_viewport_point_to_pixel_position(
                    m_layout, mouse_position, false);
            if(pixel_position) {
                m_pointer_input = ViewportPointerInput{
                    .pixel_position = *pixel_position,
                    .pointer_over_image = pointer_over_image,
                    .pressed = left_pressed,
                    .down = m_left_pointer_active
                        && ImGui::IsMouseDown(ImGuiMouseButton_Left),
                    .released = left_released
                };
            }
        }
        if(left_released) {
            m_left_pointer_active = false;
        }

        if(pointer_over_image
           && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_orbit_drag_active = true;
        }
        if(pointer_over_image
           && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            m_pan_drag_active = true;
        }
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            m_orbit_drag_active = false;
        }
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            m_pan_drag_active = false;
        }

        const Comet::Math::Vec2 orbit_delta = m_orbit_drag_active
            ? Comet::Math::Vec2(io.MouseDelta.x, io.MouseDelta.y)
            : Comet::Math::Vec2(0.0f);
        const Comet::Math::Vec2 pan_delta = m_pan_drag_active
            ? Comet::Math::Vec2(io.MouseDelta.x, io.MouseDelta.y)
            : Comet::Math::Vec2(0.0f);
        const float zoom_delta = pointer_over_image
            ? io.MouseWheel
            : 0.0f;
        if(orbit_delta == Comet::Math::Vec2(0.0f)
           && pan_delta == Comet::Math::Vec2(0.0f)
           && zoom_delta == 0.0f) {
            return;
        }

        m_camera_input = EditorCameraInput{
            .orbit_delta = orbit_delta,
            .pan_delta = pan_delta,
            .zoom_delta = zoom_delta,
            .viewport_height = m_layout.image_display_rect.size().y
        };
    }

    void ViewPanel::reset_view_interaction() {
        m_left_pointer_active = false;
        m_orbit_drag_active = false;
        m_pan_drag_active = false;
    }

    std::optional<EditorCameraInput> ViewPanel::take_camera_input() {
        return std::exchange(m_camera_input, std::nullopt);
    }

    std::optional<Comet::CameraProjection>
    ViewPanel::take_camera_projection_request() {
        return std::exchange(
            m_camera_projection_request, std::nullopt);
    }

    std::optional<ViewportPointerInput> ViewPanel::take_pointer_input() {
        return std::exchange(m_pointer_input, std::nullopt);
    }

    bool ViewPanel::take_focus_request() {
        return std::exchange(m_focus_request, false);
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
