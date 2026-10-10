#include "viewport/viewport_panel.h"
#include "ui/shortcuts.h"
#include "ui/icons.h"
#include "scene/selection.h"
#include "viewport/transform_gizmo.h"
#include "scene/scene_runtime.h"
#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>
#include <cmath>
#include <string>
#include <utility>

namespace CometEditor {
    namespace {
        constexpr std::uint32_t RESIZE_STABLE_FRAME_COUNT = 2;

        void toolbar_next(const float width, const bool new_group = false) {
            const float spacing = ImGui::GetStyle().ItemSpacing.x * (new_group ? 2 : 1);
            ImGui::SameLine(0, spacing);
            if(ImGui::GetContentRegionAvail().x < width)
                ImGui::NewLine();
        }

        bool toolbar_button(Ui::Icon icon, const char* label, const bool same_line = false) {
            if(same_line) {
                const auto& style = ImGui::GetStyle();
                const float width = ImGui::GetFontSize() + style.ItemInnerSpacing.x
                                    + ImGui::CalcTextSize(label, nullptr, true).x
                                    + style.FramePadding.x * 2;
                toolbar_next(width);
            }
            return Ui::icon_button(icon, label);
        }

        bool begin_toolbar_menu(const char* id, const char* label, const bool new_group = false) {
            const auto& style = ImGui::GetStyle();
            const float width = ImGui::CalcTextSize(label).x + ImGui::GetFontSize()
                                + style.ItemInnerSpacing.x + style.FramePadding.x * 2;
            toolbar_next(width, new_group);
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, 0.5f));
            const bool pressed = ImGui::Button((std::string(label) + id).c_str(), {width, 0});
            ImGui::PopStyleVar();
            const auto minimum = ImGui::GetItemRectMin();
            const auto maximum = ImGui::GetItemRectMax();
            if(ImGui::IsItemVisible()) {
                ImGui::RenderArrow(ImGui::GetWindowDrawList(),
                    {maximum.x - style.FramePadding.x - ImGui::GetFontSize(),
                        minimum.y + style.FramePadding.y},
                    ImGui::GetColorU32(ImGuiCol_Text), ImGuiDir_Down);
            }
            if(pressed)
                ImGui::OpenPopup(id);
            ImGui::SetNextWindowPos(
                {minimum.x, maximum.y + style.ItemSpacing.y}, ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints({ImGui::GetFontSize() * 12, 0}, {FLT_MAX, FLT_MAX});
            return ImGui::BeginPopup(id);
        }

        bool ui_blocks_runtime_input() {
            const auto& io = ImGui::GetIO();
            // 关闭方向键导航后，Ctrl+Tab 仍可进入 ImGui 窗口切换。
            return io.AppFocusLost || io.WantTextInput || GImGui->NavWindowingTarget
                   || ImGui::IsAnyItemActive() || ImGui::IsDragDropActive()
                   || ImGui::IsPopupOpen(
                       nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        }
    }

    ViewportPanel::ViewportPanel(const EditorState& state, const Comet::SceneRuntime& runtime,
        SelectionService& selection, TransformGizmo& gizmo, PropertyEditTransaction& inspector_edit,
        const std::uint32_t max_render_dimension, const EditorShortcuts& shortcuts)
        : EditorPanel("视口###Viewport"), m_state(state), m_runtime(runtime),
          m_selection(selection), m_gizmo(gizmo), m_inspector_edit(inspector_edit),
          m_shortcuts(shortcuts), m_max_render_dimension(max_render_dimension) {}

    void ViewportPanel::render() {
        m_actually_visible = false;
        m_play_image_hovered = false;
        m_camera_input.reset();
        m_camera_projection_request.reset();
        m_play_command.reset();
        m_pick_request.reset();
        m_focus_request = false;
        m_mesh_drop.reset();
        m_gizmo_draw_list = nullptr;
        const bool running = m_state.mode == EditorMode::Play
                             && m_runtime.get_state() == Comet::SceneRuntime::State::Running;
        const bool started = running && !m_runtime_was_running;
        m_runtime_was_running = running;

        if(!m_user_visible) {
            reset_hidden_view();
            return;
        }

        if(started && !ui_blocks_runtime_input())
            ImGui::SetNextWindowFocus();
        if(!ImGui::Begin(window_label().c_str(), &m_user_visible)) {
            reset_hidden_view();
            ImGui::End();
            return;
        }

        m_actually_visible = true;
        m_window_id = ImGui::GetCurrentWindow()->RootWindow->ID;
        m_interaction_id = ImGui::GetID("TransformGizmo");
        if(m_gizmo.active()) {
            ImGui::KeepAliveID(m_interaction_id);
        }

        ImGui::BeginDisabled(m_gizmo.active());
        render_toolbar();
        ImGui::EndDisabled();

        render_view_content();

        ImGui::End();
    }

    void ViewportPanel::reset_hidden_view() {
        m_layout = {};
        m_observed_render_resolution = {};
        m_requested_render_size = {};
        m_render_resolution_stable_frames = 0;
        cancel_interaction();
    }

    void ViewportPanel::render_toolbar() {
        const bool is_playing = m_state.mode == EditorMode::Play;
        if(is_playing) {
            if(toolbar_button(Ui::Icon::Stop, "停止###Stop"))
                m_play_command = PlayCommand::Stop;
            ImGui::SetItemTooltip("停止 (Esc)");
            render_runtime_controls();
            render_preview_settings();
        } else {
            if(toolbar_button(Ui::Icon::Play, "运行###Play"))
                m_play_command = PlayCommand::Play;
            render_projection_controls();
            render_gizmo_settings();
        }
        if(is_playing || m_game_ui_available)
            render_view_options();
        ImGui::Separator();
    }

    void ViewportPanel::render_runtime_controls() {
        const bool paused = m_runtime.get_state() == Comet::SceneRuntime::State::Paused;
        ImGui::BeginDisabled(!m_runtime.is_active());
        if(paused) {
            if(toolbar_button(Ui::Icon::Play, "继续###Resume", true))
                m_play_command = PlayCommand::Resume;
        } else if(toolbar_button(Ui::Icon::Pause, "暂停###Pause", true)) {
            m_play_command = PlayCommand::Pause;
        }
        if(paused) {
            if(toolbar_button(Ui::Icon::Step, "单步###Step", true))
                m_play_command = PlayCommand::Step;
        }
        ImGui::EndDisabled();
    }

    void ViewportPanel::render_gizmo_settings() {
        auto settings = m_gizmo.settings();
        constexpr const char* modes[]{"移动", "旋转", "缩放"};
        const std::string label = std::string("工具：") + modes[static_cast<int>(settings.mode)];
        if(!begin_toolbar_menu("###Tool", label.c_str()))
            return;
        // 设置保持展开，可连续调整模式、空间和吸附步长。
        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
        ImGui::SeparatorText("操作模式");
        bool changed = false;
        if(ImGui::MenuItem(
               "移动###Move", nullptr, settings.mode == TransformGizmo::Mode::Translate)) {
            settings.mode = TransformGizmo::Mode::Translate;
            changed = true;
        }
        if(ImGui::MenuItem(
               "旋转###Rotate", nullptr, settings.mode == TransformGizmo::Mode::Rotate)) {
            settings.mode = TransformGizmo::Mode::Rotate;
            changed = true;
        }
        if(ImGui::MenuItem("缩放###Scale", nullptr, settings.mode == TransformGizmo::Mode::Scale)) {
            settings.mode = TransformGizmo::Mode::Scale;
            changed = true;
        }
        ImGui::SeparatorText("坐标空间");
        if(settings.mode == TransformGizmo::Mode::Scale) {
            ImGui::MenuItem("局部（缩放）", nullptr, true, false);
        } else {
            if(ImGui::MenuItem(
                   "世界###World", nullptr, settings.space == TransformGizmo::Space::World)) {
                settings.space = TransformGizmo::Space::World;
                changed = true;
            }
            if(ImGui::MenuItem(
                   "局部###Local", nullptr, settings.space == TransformGizmo::Space::Local)) {
                settings.space = TransformGizmo::Space::Local;
                changed = true;
            }
        }
        ImGui::SeparatorText("吸附");
        changed |= ImGui::MenuItem("启用吸附###Snap", nullptr, &settings.snap);
        ImGui::BeginDisabled(!settings.snap);
        ImGui::SetNextItemWidth(120);
        if(settings.mode == TransformGizmo::Mode::Rotate)
            changed |= ImGui::InputFloat(
                "角度步长###Angle step", &settings.rotation_step_degrees, 0, 0, "%.1f");
        else if(settings.mode == TransformGizmo::Mode::Scale)
            changed |=
                ImGui::InputFloat("缩放步长###Scale step", &settings.scale_step, 0, 0, "%.2f");
        else
            changed |=
                ImGui::InputFloat("位移步长###Move step", &settings.translation_step, 0, 0, "%.3f");
        ImGui::EndDisabled();
        if(settings.mode == TransformGizmo::Mode::Rotate
            && settings.space == TransformGizmo::Space::World)
            ImGui::TextDisabled("世界旋转要求父级均匀缩放。\n非均匀缩放时请选择局部空间。");
        if(changed)
            static_cast<void>(m_gizmo.set_settings(settings));
        ImGui::PopItemFlag();
        ImGui::EndPopup();
    }

    void ViewportPanel::render_projection_controls() {
        using Projection = Comet::RenderCamera::Projection;
        const bool perspective = m_state.camera.projection == Projection::Perspective;
        if(!begin_toolbar_menu("###Projection", perspective ? "投影：3D" : "投影：2D", true)) {
            return;
        }
        for(const Projection projection : {Projection::Orthographic, Projection::Perspective}) {
            const bool selected = m_state.camera.projection == projection;
            const char* label = projection == Projection::Orthographic ? "2D 正交" : "3D 透视";
            if(ImGui::MenuItem(label, nullptr, selected))
                m_camera_projection_request = projection;
        }
        ImGui::EndPopup();
    }

    void ViewportPanel::render_preview_settings() {
        using ResolutionMode = ViewportLayout::ResolutionPolicy::Mode;
        const Comet::Math::Vec2u hd_resolution(1280, 720);
        const Comet::Math::Vec2u full_hd_resolution(1920, 1080);

        std::string resolution_label = "自由";
        if(m_play_resolution_policy.mode == ResolutionMode::Aspect16By9) {
            resolution_label = "16:9";
        } else if(m_play_resolution_policy.mode == ResolutionMode::Fixed) {
            const auto size = m_play_resolution_policy.fixed_resolution;
            resolution_label = std::to_string(size.x) + " x " + std::to_string(size.y);
        }
        const char* display_label = "1:1";
        if(m_play_display_mode == ViewportLayout::DisplayMode::Fit)
            display_label = "适应";
        const std::string label = "预览：" + resolution_label + " / " + display_label;
        if(!begin_toolbar_menu("###Preview", label.c_str(), true)) {
            return;
        }

        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
        ImGui::SeparatorText("渲染分辨率");
        if(ImGui::MenuItem(
               "自由###Free", nullptr, m_play_resolution_policy.mode == ResolutionMode::Free))
            m_play_resolution_policy = {};
        if(ImGui::MenuItem(
               "16:9", nullptr, m_play_resolution_policy.mode == ResolutionMode::Aspect16By9))
            m_play_resolution_policy = {.mode = ResolutionMode::Aspect16By9};
        if(ImGui::MenuItem("1280 x 720", nullptr,
               m_play_resolution_policy.mode == ResolutionMode::Fixed
                   && m_play_resolution_policy.fixed_resolution == hd_resolution)) {
            m_play_resolution_policy = {
                .mode = ResolutionMode::Fixed, .fixed_resolution = hd_resolution};
        }
        if(ImGui::MenuItem("1920 x 1080", nullptr,
               m_play_resolution_policy.mode == ResolutionMode::Fixed
                   && m_play_resolution_policy.fixed_resolution == full_hd_resolution)) {
            m_play_resolution_policy = {
                .mode = ResolutionMode::Fixed, .fixed_resolution = full_hd_resolution};
        }
        ImGui::SeparatorText("显示缩放");
        if(ImGui::MenuItem(
               "适应###Fit", nullptr, m_play_display_mode == ViewportLayout::DisplayMode::Fit))
            m_play_display_mode = ViewportLayout::DisplayMode::Fit;
        if(ImGui::MenuItem(
               "1:1", nullptr, m_play_display_mode == ViewportLayout::DisplayMode::OneToOne))
            m_play_display_mode = ViewportLayout::DisplayMode::OneToOne;
        ImGui::PopItemFlag();
        ImGui::EndPopup();
    }

    void ViewportPanel::render_view_options() {
        if(!begin_toolbar_menu("###View", "视图"))
            return;
        if(m_game_ui_available) {
            ImGui::SeparatorText("项目界面");
            ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
            ImGui::MenuItem("游戏 UI###Game UI", nullptr, &m_show_game_ui);
            ImGui::PopItemFlag();
            if(ImGui::MenuItem("重载 UI###Reload UI"))
                m_game_ui_reload_requested = true;
        }
        if(m_state.mode == EditorMode::Play) {
            ImGui::SeparatorText("调试");
            if(ImGui::MenuItem("输入###Input", nullptr, false, m_runtime.is_active()))
                m_play_command = PlayCommand::InputSettings;
        }
        ImGui::EndPopup();
    }

    void ViewportPanel::render_view_content() {
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
        if(m_state.mode == EditorMode::Edit && m_texture_id != ImTextureID_Invalid
            && ImGui::BeginDragDropTarget()) {
            const auto& io = ImGui::GetIO();
            const Comet::Math::Vec2 point{io.MousePos.x, io.MousePos.y};
            if(map_viewport_point_to_pixel(m_layout, point)) {
                if(const auto payload = read_asset_drag_payload(ImGui::GetDragDropPayload())) {
                    const auto& asset = *payload;
                    if(asset.type == Comet::AssetType::Mesh
                        && ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
                        const auto uv = (point - m_layout.image_display_rect.min) / display_size;
                        if(const auto position = camera_focus_plane_point(
                               m_state.camera, uv, display_size.x / display_size.y))
                            m_mesh_drop = MeshDrop{asset, *position};
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        update_view_interaction();
    }

    std::optional<ViewportPanel::MeshDrop> ViewportPanel::take_mesh_drop() {
        return std::exchange(m_mesh_drop, std::nullopt);
    }

    void ViewportPanel::update_view_interaction() {
        if(m_state.mode == EditorMode::Play) {
            if(m_gizmo.active())
                cancel_interaction();
            reset_camera_interaction();
            update_play_interaction();
            return;
        }
        if(m_state.mode != EditorMode::Edit || ImGui::IsDragDropActive()) {
            cancel_interaction();
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        const Comet::Math::Vec2 mouse_position(io.MousePos.x, io.MousePos.y);
        const auto mapped_pixel = map_viewport_point_to_pixel(m_layout, mouse_position);
        const bool pointer_over_image = ImGui::IsItemHovered() && mapped_pixel.has_value();

        if(pointer_over_image || m_gizmo.active()) {
            // Image 没有 item ID；直接指定 owner，拖动期间也阻止窗口滚动。
            ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, m_interaction_id);
        }

        const bool was_dragging = m_gizmo.active();
        const bool navigation_input = m_camera_drag || io.KeyAlt
                                      || ImGui::IsMouseDown(ImGuiMouseButton_Right)
                                      || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool available = pointer_over_image && !navigation_input && !ImGui::IsAnyItemActive()
                               && !io.WantTextInput && m_texture_id != ImTextureID_Invalid;
        bool pressed = available && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if(pressed && !m_inspector_edit.commit()) {
            pressed = false;
        }
        // Image 没有可聚焦的 item；先接收从其他面板进入的点击，再判断拖动失焦。
        if(pressed)
            ImGui::SetWindowFocus();
        const auto entity = m_selection.get_selected_entity();
        const bool consumed = m_gizmo.update(entity.get_uuid(), m_state.camera.snapshot(), m_layout,
            {
                .position = mouse_position,
                .hovered = available,
                .pressed = pressed,
                .down = ImGui::IsMouseDown(ImGuiMouseButton_Left),
                .released = ImGui::IsMouseReleased(ImGuiMouseButton_Left),
                .cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || io.AppFocusLost
                          || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
                          || ImGui::IsPopupOpen(
                              nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)
                          || m_texture_id == ImTextureID_Invalid,
            });
        if(m_gizmo.active()) {
            ImGui::SetActiveID(m_interaction_id, ImGui::GetCurrentWindow());
            ImGui::KeepAliveID(m_interaction_id);
        } else if(was_dragging && ImGui::GetActiveID() == m_interaction_id) {
            ImGui::ClearActiveID();
        }
        if(consumed) {
            reset_camera_interaction();
            return;
        }

        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput
            && !ImGui::IsAnyItemActive() && !m_camera_drag
            && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)
            && m_shortcuts.pressed(
                EditorShortcuts::Action::FocusSelection, ImGuiInputFlags_RouteGlobal)) {
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

        const bool orbit_drag = m_camera_drag && m_camera_drag->mode == CameraDragMode::Orbit;
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

    void ViewportPanel::reset_camera_interaction() {
        m_camera_drag.reset();
    }

    void ViewportPanel::cancel_interaction() {
        m_play_image_hovered = false;
        m_runtime_input = {};
        m_game_ui_input = {};
        static_cast<void>(m_gizmo.cancel());
        if(m_interaction_id != 0 && ImGui::GetActiveID() == m_interaction_id) {
            ImGui::ClearActiveID();
        }
        reset_camera_interaction();
    }

    void ViewportPanel::update_play_interaction() {
        const auto& io = ImGui::GetIO();
        m_play_image_hovered =
            m_texture_id != ImTextureID_Invalid && ImGui::IsItemHovered()
            && map_viewport_point_to_pixel(m_layout, {io.MousePos.x, io.MousePos.y}).has_value();
        if(!m_runtime.is_active() || !m_play_image_hovered || m_play_command
            || ui_blocks_runtime_input())
            return;
        // 画面拥有鼠标悬停，避免 ImGui 把游戏点击当作窗口背景拖动。
        ImGui::SetHoveredID(m_interaction_id);
        ImGui::SetWindowFocus();
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelX, m_interaction_id);
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, m_interaction_id);
    }

    bool ViewportPanel::accepts_runtime_input(const bool blocked) const {
        const auto* focused = GImGui->NavWindow;
        const auto image_size = m_layout.image_visible_rect.size();
        return m_state.mode == EditorMode::Play && m_runtime.is_active() && m_actually_visible
               && m_texture_id != ImTextureID_Invalid && image_size.x > 0 && image_size.y > 0
               && !m_play_command && focused && focused->RootWindow->ID == m_window_id && !blocked
               && !ui_blocks_runtime_input();
    }

    const Comet::Input::Frame& ViewportPanel::route_runtime_input(
        const Comet::Input::Frame& input, const bool ui_input_blocked, const bool pointer_blocked) {
        const bool blocked = ui_input_blocked || ui_blocks_runtime_input();
        if(!blocked && m_state.mode == EditorMode::Play && input.focused
            && input.key(Comet::Input::Key::Escape).pressed)
            m_play_command = PlayCommand::Stop;
        const bool accepting = accepts_runtime_input(blocked);
        const bool pointer_enabled =
            !pointer_blocked && (m_play_image_hovered || m_runtime.wants_cursor_capture());
        return m_runtime_input.read(input, accepting, pointer_enabled);
    }

    std::optional<Comet::Ui::View> ViewportPanel::game_ui_view(
        const Comet::Math::Vec2u pixel_size) const {
        const auto display = m_layout.image_display_rect;
        const auto visible = m_layout.image_visible_rect;
        if(!m_game_ui_available || !m_show_game_ui || !m_actually_visible
            || m_texture_id == ImTextureID_Invalid || display.size().x <= 0 || display.size().y <= 0
            || visible.size().x <= 0 || visible.size().y <= 0 || !pixel_size.x || !pixel_size.y)
            return std::nullopt;
        const auto window = ImGui::GetMainViewport()->Pos;
        const Comet::Math::Vec2 origin{window.x, window.y};
        return Comet::Ui::View{.origin = display.min - origin,
            .size = display.size(),
            .pixel_size = pixel_size,
            // 离屏分辨率独立于窗口 DPI；dp 在视口显示后仍对应窗口逻辑尺寸。
            .density = static_cast<float>(pixel_size.x) / display.size().x,
            .clip = Comet::Ui::View::Clip{visible.min - origin, visible.size()}};
    }

    const Comet::Input::Frame& ViewportPanel::route_game_ui_input(
        const Comet::Input::Frame& input, const bool blocked) {
        return m_game_ui_input.read(input,
            m_game_ui_available && m_show_game_ui && accepts_runtime_input(blocked),
            m_play_image_hovered);
    }

    bool ViewportPanel::take_game_ui_reload_request() {
        return std::exchange(m_game_ui_reload_requested, false);
    }

    void ViewportPanel::draw_gizmo() {
        // draw list 仅在当前 UI 帧内有效。
        ImDrawList* draw_list = std::exchange(m_gizmo_draw_list, nullptr);
        if(!draw_list || m_state.mode != EditorMode::Edit || m_pick_request || m_play_command
            || m_texture_id == ImTextureID_Invalid) {
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
            if(handle->axis == TransformGizmo::Axis::X) {
                color = IM_COL32(240, 65, 55, 255);
            } else if(handle->axis == TransformGizmo::Axis::Y) {
                color = IM_COL32(65, 225, 85, 255);
            } else if(handle->axis == TransformGizmo::Axis::All) {
                color = IM_COL32(235, 235, 235, 255);
            }
            if(m_gizmo.active_axis() == handle->axis
                || (!m_gizmo.active() && m_gizmo.hovered_axis() == handle->axis)) {
                color = IM_COL32(255, 220, 50, 255);
            }
            if(m_gizmo.settings().mode == TransformGizmo::Mode::Rotate) {
                for(const auto& segment : handle->segments)
                    draw_list->AddLine(ImVec2(segment.start.x, segment.start.y),
                        ImVec2(segment.end.x, segment.end.y), color, 2.5f);
                continue;
            }
            const auto& segment = handle->segments.front();
            if(m_gizmo.settings().mode == TransformGizmo::Mode::Scale) {
                auto point = segment.end;
                if(handle->axis == TransformGizmo::Axis::All)
                    point = (segment.start + segment.end) * 0.5f;
                else
                    draw_list->AddLine(ImVec2(segment.start.x, segment.start.y),
                        ImVec2(point.x, point.y), color, 2.5f);
                draw_list->AddRectFilled(
                    ImVec2(point.x - 4, point.y - 4), ImVec2(point.x + 4, point.y + 4), color);
                continue;
            }
            const auto direction = segment.end - segment.start;
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
            const auto unit = direction / length;
            const Comet::Math::Vec2 side(-unit.y, unit.x);
            const auto base = segment.end - unit * std::min(9.0f, length * 0.4f);
            const auto left = base + side * 4.0f;
            const auto right = base - side * 4.0f;
            draw_list->AddLine(
                ImVec2(segment.start.x, segment.start.y), ImVec2(base.x, base.y), color, 2.5f);
            draw_list->AddTriangleFilled(ImVec2(segment.end.x, segment.end.y),
                ImVec2(left.x, left.y), ImVec2(right.x, right.y), color);
        }
        draw_list->PopClipRect();
    }

    std::optional<EditorCameraInput> ViewportPanel::take_camera_input() {
        return std::exchange(m_camera_input, std::nullopt);
    }

    std::optional<Comet::RenderCamera::Projection> ViewportPanel::take_projection_request() {
        return std::exchange(m_camera_projection_request, std::nullopt);
    }

    std::optional<PlayCommand> ViewportPanel::take_play_command() {
        return std::exchange(m_play_command, std::nullopt);
    }

    std::optional<Comet::Math::Vec2u> ViewportPanel::take_pick_request() {
        return std::exchange(m_pick_request, std::nullopt);
    }

    bool ViewportPanel::take_focus_request() {
        return std::exchange(m_focus_request, false);
    }

    void ViewportPanel::set_texture_id(
        const ImTextureID texture_id, const std::uint32_t width, const std::uint32_t height) {
        m_texture_id = texture_id;
        m_texture_resolution = Comet::Math::Vec2u(width, height);
    }

    void ViewportPanel::clear_texture() {
        m_texture_id = ImTextureID_Invalid;
        m_texture_resolution = {};
    }

}
