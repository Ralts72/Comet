#pragma once
#include "viewport/camera_controller.h"
#include "editor_state.h"
#include "ui/editor_panel.h"
#include "viewport/viewport_layout.h"
#include "assets/asset_reference.h"
#include "input/input.h"
#include "ui/view.h"

#include <imgui.h>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace Comet {
    class SceneRuntime;
}

namespace CometEditor {
    class SelectionService;
    class TransformGizmo;
    class PropertyEditTransaction;
    class EditorShortcuts;
    class TerrainEditing;

    class ViewportPanel final: public EditorPanel {
    public:
        struct MeshDrop {
            AssetDragPayload asset;
            Comet::Math::Vec3 position;
        };
        ViewportPanel(const EditorState& state, const Comet::SceneRuntime& runtime,
            SelectionService& selection, TransformGizmo& gizmo,
            PropertyEditTransaction& inspector_edit, std::uint32_t max_render_dimension,
            const EditorShortcuts& shortcuts);

        void render() override;

        // 在 UI 帧结束前，用处理完相机输入后的状态追加操作手柄。
        void draw_gizmo();
        void set_terrain_editing(TerrainEditing* editing) { m_terrain = editing; }
        [[nodiscard]] bool terrain_tool() const { return m_terrain_tool; }
        void cancel_interaction();

        void set_texture_id(ImTextureID texture_id, std::uint32_t width, std::uint32_t height);

        void clear_texture();

        [[nodiscard]] Comet::Math::Vec2u get_requested_render_size() const {
            return m_requested_render_size;
        }

        [[nodiscard]] const ViewportLayout& get_layout() const { return m_layout; }

        [[nodiscard]] bool is_visible() const { return m_actually_visible; }
        [[nodiscard]] bool shows_colliders() const { return m_show_colliders; }
        [[nodiscard]] bool shows_joints() const { return m_show_joints; }
        [[nodiscard]] Comet::IrradianceDebugMode irradiance_debug() const {
            return m_irradiance_debug;
        }
        [[nodiscard]] bool path_trace_enabled() const { return m_path_trace; }
        [[nodiscard]] const Comet::PathTraceSettings& path_trace_settings() const {
            return m_path_trace_settings;
        }
        bool take_path_trace_restart() { return std::exchange(m_path_trace_restart, false); }
        bool take_path_trace_export() { return std::exchange(m_path_trace_export, false); }
        void set_path_trace_status(uint32_t samples, std::string error) {
            m_path_trace_samples = samples;
            m_path_trace_error = std::move(error);
        }

        // 所有面板绘制后复核焦点／弹窗，避免当帧输入穿透到运行场景。
        [[nodiscard]] const Comet::Input::Frame& route_runtime_input(
            const Comet::Input::Frame& input, bool ui_input_blocked = false,
            bool pointer_blocked = false);

        void set_game_ui_available(bool available) { m_game_ui_available = available; }
        void set_game_ui_error(std::string error) { m_game_ui_error = std::move(error); }
        void set_play_resolution(Comet::Math::Vec2u size) {
            m_play_resolution_policy = {
                .mode = ViewportLayout::ResolutionPolicy::Mode::Fixed, .fixed_resolution = size};
        }
        [[nodiscard]] std::optional<Comet::Ui::View> game_ui_view(
            Comet::Math::Vec2u pixel_size) const;
        [[nodiscard]] const Comet::Input::Frame& route_game_ui_input(
            const Comet::Input::Frame& input, bool blocked);
        [[nodiscard]] bool take_game_ui_reload_request();

        [[nodiscard]] std::optional<EditorCameraInput> take_camera_input();

        [[nodiscard]] std::optional<Comet::RenderCamera::Projection> take_projection_request();

        [[nodiscard]] std::optional<PlayCommand> take_play_command();

        [[nodiscard]] std::optional<Comet::Math::Vec2u> take_pick_request();

        [[nodiscard]] bool take_focus_request();
        [[nodiscard]] std::optional<MeshDrop> take_mesh_drop();

    private:
        enum class CameraDragMode { Orbit, Pan };

        struct CameraDrag {
            CameraDragMode mode;
            ImGuiMouseButton button;
        };

        void render_toolbar();
        void render_runtime_controls();
        void render_projection_controls();
        void render_gizmo_settings();
        void render_preview_settings();
        void render_view_options();
        void render_view_content();
        void update_view_interaction();
        void update_play_interaction();
        void reset_hidden_view();
        void reset_camera_interaction();
        [[nodiscard]] bool accepts_runtime_input(bool blocked) const;

        TerrainEditing* m_terrain = nullptr;
        bool m_terrain_tool = false;
        const EditorState& m_state;
        const Comet::SceneRuntime& m_runtime;
        SelectionService& m_selection;
        TransformGizmo& m_gizmo;
        PropertyEditTransaction& m_inspector_edit;
        const EditorShortcuts& m_shortcuts;
        ImGuiID m_interaction_id = 0;
        ImDrawList* m_gizmo_draw_list = nullptr;
        bool m_actually_visible = false;
        ImGuiID m_window_id = 0;
        bool m_play_image_hovered = false;
        bool m_runtime_was_running = false;
        Comet::Input::Gate m_runtime_input;
        Comet::Input::Gate m_game_ui_input;
        bool m_game_ui_available = false;
        std::string m_game_ui_error;
        bool m_show_game_ui = true;
        bool m_show_colliders = false;
        bool m_show_joints = false;
        Comet::IrradianceDebugMode m_irradiance_debug = Comet::IrradianceDebugMode::Off;
        bool m_game_ui_reload_requested = false;
        bool m_path_trace = false;
        bool m_path_trace_restart = false;
        bool m_path_trace_export = false;
        Comet::PathTraceSettings m_path_trace_settings;
        uint32_t m_path_trace_samples = 0;
        std::string m_path_trace_error;
        std::uint32_t m_max_render_dimension = 0;
        ViewportLayout::ResolutionPolicy m_play_resolution_policy;
        ViewportLayout::DisplayMode m_play_display_mode = ViewportLayout::DisplayMode::Fit;

        ImTextureID m_texture_id = ImTextureID_Invalid;
        Comet::Math::Vec2u m_texture_resolution{};
        ViewportLayout m_layout;
        Comet::Math::Vec2u m_observed_render_resolution{};
        Comet::Math::Vec2u m_requested_render_size{};
        std::uint32_t m_render_resolution_stable_frames = 0;
        std::optional<EditorCameraInput> m_camera_input;
        std::optional<Comet::RenderCamera::Projection> m_camera_projection_request;
        std::optional<PlayCommand> m_play_command;
        std::optional<Comet::Math::Vec2u> m_pick_request;
        bool m_focus_request = false;
        std::optional<CameraDrag> m_camera_drag;
        std::optional<MeshDrop> m_mesh_drop;
    };
}
