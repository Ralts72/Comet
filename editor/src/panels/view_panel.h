#pragma once
#include "editor_panel.h"
#include <imgui.h>

namespace CometEditor {

    enum class ViewType {
        SceneView,
        GameView
    };

    class ViewPanel : public EditorPanel {
    public:
        explicit ViewPanel(ViewType view_type);

        void render() override;

        [[nodiscard]] ViewType get_view_type() const { return m_view_type; }

        [[nodiscard]] bool is_2d_mode() const { return m_2d_mode; }
        void set_2d_mode(bool is_2d) { m_2d_mode = is_2d; }

        void set_texture_id(ImTextureID texture_id, uint32_t width, uint32_t height);

        void clear_texture();

    private:
        void render_view_content();

        ViewType m_view_type;
        bool m_2d_mode = false;

        ImTextureID m_texture_id = ImTextureID_Invalid;
        uint32_t m_texture_width = 0;
        uint32_t m_texture_height = 0;
    };
}

