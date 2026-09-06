#include "view.h"
#include <imgui.h>

#include <utility>

namespace CometEditor {
    namespace {
        constexpr std::uint32_t RESIZE_STABLE_FRAME_COUNT = 2;
        constexpr float TOOLBAR_BUTTON_WIDTH = 40.0f;
    }

    ViewPanel::ViewPanel(
        const EditorState& state, const std::uint32_t max_render_dimension)
        : EditorPanel("Viewport"), m_state(state),
          m_max_render_dimension(max_render_dimension) {}

    void ViewPanel::render() {
        m_actually_visible = false;
        m_camera_input.reset();
        m_camera_projection_request.reset();
        m_mode_request.reset();
        m_pick_request.reset();

        if(!m_user_visible) {
            m_layout = {};
            m_observed_render_resolution = {};
            m_requested_render_size = {};
            m_render_resolution_stable_frames = 0;
            reset_camera_interaction();
            return;
        }

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            m_layout = {};
            m_observed_render_resolution = {};
            m_requested_render_size = {};
            m_render_resolution_stable_frames = 0;
            reset_camera_interaction();
            ImGui::End();
            return;
        }

        if(ImGui::IsWindowCollapsed()) {
            m_layout = {};
            m_observed_render_resolution = {};
            m_requested_render_size = {};
            m_render_resolution_stable_frames = 0;
            reset_camera_interaction();
            ImGui::End();
            return;
        }

        m_actually_visible = true;

        render_toolbar();

        render_view_content();

        ImGui::End();
    }

    void ViewPanel::render_toolbar() {
        const bool is_playing = m_state.mode == EditorMode::Play;
        const ImVec2 button_size(TOOLBAR_BUTTON_WIDTH, ImGui::GetFrameHeight());
        ImGui::AlignTextToFramePadding();
        if(is_playing) {
            ImGui::TextUnformatted("Play (Scene Camera)");
        } else {
            ImGui::TextUnformatted("Edit (Editor Camera)");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::BeginDisabled(is_playing);
        render_projection_controls();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::BeginDisabled(is_playing);
        if(ImGui::Button("Play", button_size)) {
            m_mode_request = EditorMode::Play;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!is_playing);
        if(ImGui::Button("Stop", button_size)) {
            m_mode_request = EditorMode::Edit;
        }
        ImGui::EndDisabled();

        if(is_playing) {
            ImGui::SameLine();
            render_play_toolbar();
        }
        ImGui::Separator();
    }

    void ViewPanel::render_projection_controls() {
        using Projection = Comet::RenderCamera::Projection;
        const ImVec2 button_size(TOOLBAR_BUTTON_WIDTH, ImGui::GetFrameHeight());
        for(const Projection projection :
            {Projection::Orthographic, Projection::Perspective}) {
            if(projection == Projection::Perspective) {
                ImGui::SameLine();
            }
            const bool selected = m_state.camera.projection == projection;
            if(selected) {
                ImGui::PushStyleColor(
                    ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            const char* label = projection == Projection::Orthographic ? "2D" : "3D";
            if(ImGui::Button(label, button_size)) {
                m_camera_projection_request = projection;
            }
            if(selected) {
                ImGui::PopStyleColor();
            }
        }
    }

    void ViewPanel::render_play_toolbar() {
        using ResolutionMode = ViewportLayout::ResolutionPolicy::Mode;
        const float dropdown_width = TOOLBAR_BUTTON_WIDTH + ImGui::GetFrameHeight();
        const Comet::Math::Vec2u hd_resolution(1280, 720);
        const Comet::Math::Vec2u full_hd_resolution(1920, 1080);

        const char* resolution_label = "Free";
        if(m_play_resolution_policy.mode == ResolutionMode::Aspect16By9) {
            resolution_label = "16:9";
        } else if(m_play_resolution_policy.mode == ResolutionMode::Fixed) {
            if(m_play_resolution_policy.fixed_resolution == hd_resolution) {
                resolution_label = "HD";
            } else if(m_play_resolution_policy.fixed_resolution == full_hd_resolution) {
                resolution_label = "FHD";
            } else {
                resolution_label = "User";
            }
        }

        ImGui::SetNextItemWidth(dropdown_width);
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
        ImGui::SetNextItemWidth(dropdown_width);
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
    }

    void ViewPanel::render_view_content() {
        const ImVec2 content_size = ImGui::GetContentRegionAvail();
        const ImVec2 content_origin = ImGui::GetCursorScreenPos();
        const ImGuiViewport* window_viewport = ImGui::GetWindowViewport();
        const ImVec2 framebuffer_scale =
            window_viewport ? window_viewport->FramebufferScale : ImVec2(1.0f, 1.0f);

        ViewportLayout::ResolutionPolicy resolution_policy;
        ViewportLayout::DisplayMode display_mode = ViewportLayout::DisplayMode::Fit;
        if(m_state.mode == EditorMode::Play) {
            resolution_policy = m_play_resolution_policy;
            display_mode = m_play_display_mode;
        }
        m_layout = calculate_viewport_layout({
            .content_origin = {content_origin.x, content_origin.y},
            .content_size = {content_size.x, content_size.y},
            .framebuffer_scale = {framebuffer_scale.x, framebuffer_scale.y},
            .current_render_resolution = m_texture_resolution,
            .max_render_dimension = m_max_render_dimension,
            .resolution_policy = resolution_policy,
            .display_mode = display_mode,
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
            reset_camera_interaction();
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
        update_view_interaction();
    }

    void ViewPanel::update_view_interaction() {
        if(m_state.mode != EditorMode::Edit) {
            reset_camera_interaction();
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        const Comet::Math::Vec2 mouse_position(io.MousePos.x, io.MousePos.y);
        const auto mapped_pixel = map_viewport_point_to_pixel(m_layout, mouse_position);
        const bool pointer_over_image =
            ImGui::IsItemHovered() && mapped_pixel.has_value();

        if(pointer_over_image && m_texture_id != ImTextureID_Invalid && !m_camera_drag
            && !ImGui::IsKeyDown(ImGuiMod_Alt)
            && !ImGui::IsMouseDown(ImGuiMouseButton_Right)
            && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_pick_request = *mapped_pixel;
        }

        if(pointer_over_image) {
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        }

        if(!m_camera_drag && pointer_over_image) {
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                m_camera_drag = CameraDrag{
                    .mode = CameraDragMode::Orbit,
                    .button = ImGuiMouseButton_Right,
                };
            } else if(ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                m_camera_drag = CameraDrag{
                    .mode = CameraDragMode::Pan,
                    .button = ImGuiMouseButton_Middle,
                };
            } else if(ImGui::IsKeyDown(ImGuiMod_Alt)
                      && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                CameraDragMode drag_mode = CameraDragMode::Orbit;
                if(ImGui::IsKeyDown(ImGuiMod_Shift)) {
                    drag_mode = CameraDragMode::Pan;
                }
                m_camera_drag = CameraDrag{
                    .mode = drag_mode,
                    .button = ImGuiMouseButton_Left,
                };
            }
        }

        if(m_camera_drag && !ImGui::IsMouseDown(m_camera_drag->button)) {
            m_camera_drag.reset();
        }

        const bool orbit_drag =
            m_camera_drag && m_camera_drag->mode == CameraDragMode::Orbit;
        const bool pan_drag = m_camera_drag && m_camera_drag->mode == CameraDragMode::Pan;
        Comet::Math::Vec2 orbit_delta(0.0f);
        if(orbit_drag) {
            orbit_delta = Comet::Math::Vec2(io.MouseDelta.x, io.MouseDelta.y);
        }
        Comet::Math::Vec2 pan_delta(0.0f);
        if(pan_drag) {
            pan_delta = Comet::Math::Vec2(io.MouseDelta.x, io.MouseDelta.y);
        }
        const float zoom_delta = pointer_over_image ? io.MouseWheel : 0.0f;
        if(orbit_delta == Comet::Math::Vec2(0.0f) && pan_delta == Comet::Math::Vec2(0.0f)
            && zoom_delta == 0.0f) {
            return;
        }

        m_camera_input = EditorCameraInput{
            .orbit_delta = orbit_delta,
            .pan_delta = pan_delta,
            .zoom_delta = zoom_delta,
            .viewport_height = m_layout.image_display_rect.size().y,
        };
    }

    void ViewPanel::reset_camera_interaction() {
        m_camera_drag.reset();
    }

    std::optional<EditorCameraInput> ViewPanel::take_camera_input() {
        return std::exchange(m_camera_input, std::nullopt);
    }

    std::optional<Comet::RenderCamera::Projection> ViewPanel::take_projection_request() {
        return std::exchange(m_camera_projection_request, std::nullopt);
    }

    std::optional<EditorMode> ViewPanel::take_mode_request() {
        return std::exchange(m_mode_request, std::nullopt);
    }

    std::optional<Comet::Math::Vec2u> ViewPanel::take_pick_request() {
        return std::exchange(m_pick_request, std::nullopt);
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
