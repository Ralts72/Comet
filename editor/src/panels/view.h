#pragma once
#include "editor.h"
#include "core/math_utils.h"

#include <imgui.h>
#include <cstdint>

namespace CometEditor {
    enum class ViewType {
        SceneView,
        GameView
    };

    class ViewPanel: public EditorPanel {
    public:
        explicit ViewPanel(ViewType view_type);

        void render() override;

        [[nodiscard]] ViewType get_view_type() const {
            return m_view_type;
        }

        [[nodiscard]] bool is_2d_mode() const {
            return m_2d_mode;
        }

        void set_2d_mode(const bool is_2d) {
            m_2d_mode = is_2d;
        }

        void set_texture_id(ImTextureID texture_id, std::uint32_t width, std::uint32_t height);

        void clear_texture();

        [[nodiscard]] Comet::Math::Vec2u get_viewport_size() const {
            return m_viewport_size;
        }

    private:
        void render_view_content();

        ViewType m_view_type;
        bool m_2d_mode = false;

        ImTextureID m_texture_id = ImTextureID_Invalid;
        std::uint32_t m_texture_width = 0;
        std::uint32_t m_texture_height = 0;
        Comet::Math::Vec2u m_viewport_size = Comet::Math::Vec2u(0);
    };
}
