#include "view.h"
#include "selection.h"
#include "translation_gizmo.h"
#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <utility>

namespace CometEditor {
    namespace {
        constexpr std::uint32_t RESIZE_STABLE_FRAME_COUNT = 2;
        constexpr float TOOLBAR_BUTTON_WIDTH = 40.0f;
    }

    ViewPanel::ViewPanel(const EditorState& state, SelectionService& selection,
        TranslationGizmo& gizmo, PropertyEditTransaction& inspector_edit,
        const std::uint32_t max_render_dimension)
        : EditorPanel("Viewport"), m_state(state), m_selection(selection), m_gizmo(gizmo),
          m_inspector_edit(inspector_edit), m_max_render_dimension(max_render_dimension) {
    }

    void ViewPanel::render() {
        m_actually_visible = false;
        m_camera_input.reset();
        m_camera_projection_request.reset();
        m_mode_request.reset();
        m_pick_request.reset();
        m_focus_request = false;
        m_mesh_drop.reset();
        m_gizmo_draw_list = nullptr;

        if(!m_user_visible) {
            m_layout = {};
            m_observed_render_resolution = {};
            m_requested_render_size = {};
            m_render_resolution_stable_frames = 0;
            cancel_interaction();
            return;
        }

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            m_layout = {};
            m_observed_render_resolution = {};
            m_requested_render_size = {};
            m_render_resolution_stable_frames = 0;
            cancel_interaction();
            ImGui::End();
            return;
        }

        if(ImGui::IsWindowCollapsed()) {
            m_layout = {};
            m_observed_render_resolution = {};
            m_requested_render_size = {};
            m_render_resolution_stable_frames = 0;
            cancel_interaction();
            ImGui::End();
            return;
        }

        m_actually_visible = true;
        m_gizmo_id = ImGui::GetID("TranslationGizmo");
        if(m_gizmo.active()) {
            ImGui::KeepAliveID(m_gizmo_id);
        }

        ImGui::BeginDisabled(m_gizmo.active());
        render_toolbar();
        ImGui::EndDisabled();

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
        } else {
            ImGui::SameLine();
            render_gizmo_settings();
        }
        ImGui::Separator();
    }

    void ViewPanel::render_gizmo_settings() {
        if(ImGui::Button("Tool", ImVec2(TOOLBAR_BUTTON_WIDTH, ImGui::GetFrameHeight())))
            ImGui::OpenPopup("Gizmo Settings");
        if(!ImGui::BeginPopup("Gizmo Settings"))
            return;
        auto settings = m_gizmo.settings();
        int space = static_cast<int>(settings.space);
        ImGui::SetNextItemWidth(120);
        bool changed = ImGui::Combo("Space", &space, "World\0Local\0");
        settings.space = static_cast<TranslationGizmo::Space>(space);
        changed |= ImGui::Checkbox("Snap", &settings.snap);
        ImGui::BeginDisabled(!settings.snap);
        ImGui::SetNextItemWidth(120);
        changed |= ImGui::InputFloat("Step", &settings.step, 0, 0, "%.3f");
        ImGui::EndDisabled();
        if(changed)
            static_cast<void>(m_gizmo.set_settings(settings));
        ImGui::EndPopup();
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
            cancel_interaction();
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
        m_gizmo_draw_list = ImGui::GetWindowDrawList();
        if(m_state.mode == EditorMode::Edit && ImGui::BeginDragDropTarget()) {
            const auto& io = ImGui::GetIO();
            const Comet::Math::Vec2 point{io.MousePos.x, io.MousePos.y};
            if(map_viewport_point_to_pixel(m_layout, point)) {
                if(const auto* payload = ImGui::GetDragDropPayload();
                    payload && payload->IsDataType(AssetDragPayload::TYPE)) {
                    if(payload->DataSize == sizeof(AssetDragPayload)
                        && static_cast<const AssetDragPayload*>(payload->Data)->type
                               == Comet::AssetType::Mesh
                        && ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
                        const auto uv =
                            (point - m_layout.image_display_rect.min) / display_size;
                        if(const auto position = camera_focus_plane_point(
                               m_state.camera, uv, display_size.x / display_size.y))
                            m_mesh_drop = MeshDrop{
                                *static_cast<const AssetDragPayload*>(payload->Data),
                                *position};
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        update_view_interaction();
    }

    std::optional<ViewPanel::MeshDrop> ViewPanel::take_mesh_drop() {
        return std::exchange(m_mesh_drop, std::nullopt);
    }

    void ViewPanel::update_view_interaction() {
        if(m_state.mode != EditorMode::Edit || ImGui::IsDragDropActive()) {
            cancel_interaction();
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        const Comet::Math::Vec2 mouse_position(io.MousePos.x, io.MousePos.y);
        const auto mapped_pixel = map_viewport_point_to_pixel(m_layout, mouse_position);
        const bool pointer_over_image =
            ImGui::IsItemHovered() && mapped_pixel.has_value();

        if(pointer_over_image || m_gizmo.active()) {
            // Image 没有 item ID；直接指定 owner，拖动期间也阻止窗口滚动。
            ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, m_gizmo_id);
        }

        const bool was_dragging = m_gizmo.active();
        const bool navigation_input = m_camera_drag || io.KeyAlt
                                      || ImGui::IsMouseDown(ImGuiMouseButton_Right)
                                      || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool available = pointer_over_image && !navigation_input
                               && !ImGui::IsAnyItemActive() && !io.WantTextInput
                               && m_texture_id != ImTextureID_Invalid;
        bool pressed = available && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if(pressed && !m_inspector_edit.commit()) {
            pressed = false;
        }
        const auto entity = m_selection.get_selected_entity();
        const bool consumed =
            m_gizmo.update(entity.get_uuid(), m_state.camera.snapshot(), m_layout,
                {
                    .position = mouse_position,
                    .hovered = available,
                    .pressed = pressed,
                    .down = ImGui::IsMouseDown(ImGuiMouseButton_Left),
                    .released = ImGui::IsMouseReleased(ImGuiMouseButton_Left),
                    .cancel =
                        ImGui::IsKeyPressed(ImGuiKey_Escape, false) || io.AppFocusLost
                        || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
                        || ImGui::IsPopupOpen(nullptr,
                            ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)
                        || m_texture_id == ImTextureID_Invalid,
                });
        if(m_gizmo.active()) {
            ImGui::SetActiveID(m_gizmo_id, ImGui::GetCurrentWindow());
            ImGui::KeepAliveID(m_gizmo_id);
        } else if(was_dragging && ImGui::GetActiveID() == m_gizmo_id) {
            ImGui::ClearActiveID();
        }
        if(consumed) {
            reset_camera_interaction();
            return;
        }

        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
            && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper
            && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            m_focus_request = true;
        }

        if(pressed) {
            m_pick_request = *mapped_pixel;
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

    void ViewPanel::cancel_interaction() {
        static_cast<void>(m_gizmo.cancel());
        if(m_gizmo_id != 0 && ImGui::GetActiveID() == m_gizmo_id) {
            ImGui::ClearActiveID();
        }
        reset_camera_interaction();
    }

    void ViewPanel::draw_gizmo() {
        // draw list 只借用到当前 UI 帧结束，不跨帧保存或交给渲染线程。
        ImDrawList* draw_list = std::exchange(m_gizmo_draw_list, nullptr);
        if(!draw_list || m_state.mode != EditorMode::Edit || m_pick_request
            || m_mode_request || m_texture_id == ImTextureID_Invalid) {
            return;
        }
        const auto entity = m_selection.get_selected_entity();
        const auto handles =
            m_gizmo.handles(entity.get_uuid(), m_state.camera.snapshot(), m_layout);
        const auto& clip = m_layout.image_visible_rect;
        draw_list->PushClipRect(
            ImVec2(clip.min.x, clip.min.y), ImVec2(clip.max.x, clip.max.y), true);
        for(const auto& handle : handles) {
            if(!handle) {
                continue;
            }
            ImU32 color = IM_COL32(65, 125, 255, 255);
            if(handle->axis == TranslationGizmo::Axis::X) {
                color = IM_COL32(240, 65, 55, 255);
            } else if(handle->axis == TranslationGizmo::Axis::Y) {
                color = IM_COL32(65, 225, 85, 255);
            }
            if(m_gizmo.active_axis() == handle->axis
                || (!m_gizmo.active() && m_gizmo.hovered_axis() == handle->axis)) {
                color = IM_COL32(255, 220, 50, 255);
            }
            const auto direction = handle->end - handle->start;
            const float length =
                std::sqrt(direction.x * direction.x + direction.y * direction.y);
            const auto unit = direction / length;
            const Comet::Math::Vec2 side(-unit.y, unit.x);
            const auto base = handle->end - unit * std::min(9.0f, length * 0.4f);
            const auto left = base + side * 4.0f;
            const auto right = base - side * 4.0f;
            draw_list->AddLine(ImVec2(handle->start.x, handle->start.y),
                ImVec2(base.x, base.y), color, 2.5f);
            draw_list->AddTriangleFilled(ImVec2(handle->end.x, handle->end.y),
                ImVec2(left.x, left.y), ImVec2(right.x, right.y), color);
        }
        draw_list->PopClipRect();
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

    bool ViewPanel::take_focus_request() {
        return std::exchange(m_focus_request, false);
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
