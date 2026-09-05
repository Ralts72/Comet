#pragma once
#include "camera_controller.h"
#include "editor_state.h"
#include "editor.h"
#include "viewport_layout.h"

#include <imgui.h>
#include <cstdint>
#include <optional>

namespace CometEditor {
    class ViewPanel: public EditorPanel {
    public:
        ViewPanel(const EditorState& state, std::uint32_t max_render_dimension);

        void render() override;

        [[nodiscard]] bool is_2d_mode() const { return m_2d_mode; }

        void set_2d_mode(const bool is_2d) { m_2d_mode = is_2d; }

        void set_texture_id(
            ImTextureID texture_id, std::uint32_t width, std::uint32_t height);

        void clear_texture();

        [[nodiscard]] Comet::Math::Vec2u get_requested_render_size() const {
            return m_requested_render_size;
        }

        [[nodiscard]] const ViewportLayout& get_layout() const { return m_layout; }

        [[nodiscard]] std::optional<EditorCameraInput> take_camera_input();

    private:
        enum class CameraDragMode { Orbit, Pan };

        struct CameraDrag {
            CameraDragMode mode;
            ImGuiMouseButton button;
        };

        void render_edit_toolbar();
        void render_play_toolbar();
        void render_view_content();
        void update_camera_input();
        void reset_camera_interaction();

        const EditorState& m_state;
        std::uint32_t m_max_render_dimension = 0;
        bool m_2d_mode = false;
        ViewportLayout::ResolutionPolicy m_play_resolution_policy;
        ViewportLayout::DisplayMode m_play_display_mode =
            ViewportLayout::DisplayMode::Fit;

        ImTextureID m_texture_id = ImTextureID_Invalid;
        Comet::Math::Vec2u m_texture_resolution{};
        ViewportLayout m_layout;
        Comet::Math::Vec2u m_observed_render_resolution{};
        Comet::Math::Vec2u m_requested_render_size{};
        std::uint32_t m_render_resolution_stable_frames = 0;
        std::optional<EditorCameraInput> m_camera_input;
        std::optional<CameraDrag> m_camera_drag;
    };
}
