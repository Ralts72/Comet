#pragma once
#include "editor_panel.h"
#include <imgui.h>

namespace CometEditor {

    class ViewportPanel : public EditorPanel {
    public:
        ViewportPanel();
        
        void render() override;
        
        [[nodiscard]] bool is_2d_mode() const { return m_viewport_mode_2d; }
        void set_2d_mode(const bool is_2d) { m_viewport_mode_2d = is_2d; }
        
        void set_texture_id(ImTextureID texture_id, uint32_t width, uint32_t height);

    private:
        bool m_viewport_mode_2d = false;
        ImTextureID m_texture_id = ImTextureID_Invalid;
        uint32_t m_texture_width = 0;
        uint32_t m_texture_height = 0;
    };
}

