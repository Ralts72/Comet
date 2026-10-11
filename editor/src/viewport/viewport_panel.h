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

namespace Comet {
    class SceneRuntime;
}

namespace CometEditor {
    class SelectionService;
    class TransformGizmo;
    class PropertyEditTransaction;
    class EditorShortcuts;

    // 共享图像展示、DPI 坐标和尺寸稳定策略；交互由具体面板负责。
    class ViewportPanel: public EditorPanel {
    public:
        void render() override;
        virtual void cancel_interaction() = 0;
        void set_texture_id(ImTextureID texture_id, std::uint32_t width, std::uint32_t height);
        void clear_texture();
        [[nodiscard]] Comet::Math::Vec2u get_requested_render_size() const {
            return m_requested_render_size;
        }
        [[nodiscard]] const ViewportLayout& get_layout() const { return m_layout; }
        [[nodiscard]] bool is_visible() const { return m_actually_visible; }

    protected:
        ViewportPanel(
            const EditorState& state, std::uint32_t max_render_dimension, const char* label);
        virtual void begin_frame() = 0;
        virtual void render_toolbar() = 0;
        virtual void update_view_interaction() = 0;
        [[nodiscard]] virtual bool interaction_active() const { return false; }
        const EditorState& m_state;
        ImGuiID m_interaction_id = 0;
        ImGuiID m_window_id = 0;
        bool m_actually_visible = false;
        ImTextureID m_texture_id = ImTextureID_Invalid;
        Comet::Math::Vec2u m_texture_resolution{};
        ViewportLayout m_layout;
        ViewportLayout::ResolutionPolicy m_resolution_policy;
        ViewportLayout::DisplayMode m_display_mode = ViewportLayout::DisplayMode::Fit;

    private:
        void render_view_content();
        void reset_hidden_view();
        std::uint32_t m_max_render_dimension;
        Comet::Math::Vec2u m_observed_render_resolution{};
        Comet::Math::Vec2u m_requested_render_size{};
        std::uint32_t m_render_resolution_stable_frames = 0;
    };

    class SceneViewportPanel final: public ViewportPanel {
    public:
        struct MeshDrop {
            AssetDragPayload asset;
            Comet::Math::Vec3 position;
        };
        SceneViewportPanel(const EditorState& state, SelectionService& selection,
            TransformGizmo& gizmo, PropertyEditTransaction& inspector_edit,
            std::uint32_t max_render_dimension, const EditorShortcuts& shortcuts);
        void cancel_interaction() override;
        void draw_gizmo();
        [[nodiscard]] std::optional<EditorCameraInput> take_camera_input();
        [[nodiscard]] std::optional<Comet::RenderCamera::Projection> take_projection_request();
        [[nodiscard]] std::optional<Comet::Math::Vec2u> take_pick_request();
        [[nodiscard]] bool take_focus_request();
        [[nodiscard]] std::optional<MeshDrop> take_mesh_drop();

    private:
        enum class CameraDragMode { Orbit, Pan };
        struct CameraDrag {
            CameraDragMode mode;
            ImGuiMouseButton button;
        };
        void begin_frame() override;
        void render_toolbar() override;
        void update_view_interaction() override;
        [[nodiscard]] bool interaction_active() const override;
        void render_projection_controls();
        void render_gizmo_settings();
        void accept_mesh_drop();
        void reset_camera_interaction();
        SelectionService& m_selection;
        TransformGizmo& m_gizmo;
        PropertyEditTransaction& m_inspector_edit;
        const EditorShortcuts& m_shortcuts;
        ImDrawList* m_gizmo_draw_list = nullptr;
        std::optional<EditorCameraInput> m_camera_input;
        std::optional<Comet::RenderCamera::Projection> m_camera_projection_request;
        std::optional<Comet::Math::Vec2u> m_pick_request;
        bool m_focus_request = false;
        std::optional<CameraDrag> m_camera_drag;
        std::optional<MeshDrop> m_mesh_drop;
    };

    class GameViewportPanel final: public ViewportPanel {
    public:
        GameViewportPanel(const EditorState& state, const Comet::SceneRuntime& runtime,
            std::uint32_t max_render_dimension);
        void cancel_interaction() override;
        // 所有面板绘制后复核焦点和弹窗，避免输入穿透。
        [[nodiscard]] const Comet::Input::Frame& route_runtime_input(
            const Comet::Input::Frame& input, bool ui_input_blocked = false,
            bool pointer_blocked = false);
        void set_game_ui_available(bool available) { m_game_ui_available = available; }
        void set_play_resolution(Comet::Math::Vec2u size) {
            m_resolution_policy = {
                .mode = ViewportLayout::ResolutionPolicy::Mode::Fixed, .fixed_resolution = size};
        }
        [[nodiscard]] std::optional<Comet::Ui::View> game_ui_view(
            Comet::Math::Vec2u pixel_size) const;
        [[nodiscard]] const Comet::Input::Frame& route_game_ui_input(
            const Comet::Input::Frame& input, bool blocked);
        [[nodiscard]] bool take_game_ui_reload_request();
        [[nodiscard]] std::optional<PlayCommand> take_play_command();

    private:
        void begin_frame() override;
        void render_toolbar() override;
        void update_view_interaction() override;
        void render_runtime_controls();
        void render_preview_settings();
        void render_view_options();
        [[nodiscard]] bool accepts_runtime_input(bool blocked) const;
        const Comet::SceneRuntime& m_runtime;
        bool m_play_image_hovered = false;
        bool m_runtime_was_running = false;
        Comet::Input::Gate m_runtime_input;
        Comet::Input::Gate m_game_ui_input;
        bool m_game_ui_available = false;
        bool m_show_game_ui = true;
        bool m_game_ui_reload_requested = false;
        std::optional<PlayCommand> m_play_command;
    };
}
