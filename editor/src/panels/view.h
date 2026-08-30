#pragma once
#include "editor_state.h"
#include "editor_camera_controller.h"
#include "editor.h"
#include "viewport_layout.h"

#include <imgui.h>
#include <cstdint>
#include <optional>

namespace CometEditor {
    class ViewPanel: public EditorPanel {
    public:
        ViewPanel(
            const EditorState& state,
            std::uint32_t max_render_dimension);

        void render() override;

        void set_texture_id(ImTextureID texture_id, std::uint32_t width, std::uint32_t height);

        void clear_texture();

        [[nodiscard]] Comet::Math::Vec2u get_render_resolution() const {
            return m_layout.render_resolution;
        }

        [[nodiscard]] const ViewportLayout& get_layout() const {
            return m_layout;
        }

        [[nodiscard]] std::optional<EditorCameraInput>
        take_camera_input();

        [[nodiscard]] std::optional<Comet::CameraProjection>
        take_camera_projection_request();

        [[nodiscard]] std::optional<Comet::Math::Vec2u>
        take_pick_request();

    private:
        void render_edit_toolbar();
        void render_play_toolbar();
        void render_view_content();
        void update_view_interaction();
        void reset_camera_interaction();

        const EditorState& m_state;
        std::uint32_t m_max_render_dimension = 0;
        ViewportResolutionPolicy m_play_resolution_policy;
        ViewportDisplayMode m_play_display_mode = ViewportDisplayMode::Fit;

        ImTextureID m_texture_id = ImTextureID_Invalid;
        Comet::Math::Vec2u m_texture_resolution{};
        ViewportLayout m_layout;
        std::optional<EditorCameraInput> m_camera_input;
        std::optional<Comet::CameraProjection>
            m_camera_projection_request;
        std::optional<Comet::Math::Vec2u> m_pick_request;
        bool m_orbit_drag_active = false;
        bool m_pan_drag_active = false;
    };
}
