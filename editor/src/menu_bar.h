#pragma once
#include "pch.h"

namespace CometEditor {

    using PanelVisibilityCallback = std::function<void(bool)>;

    struct MenuItem {
        std::string label;
        std::string shortcut;
        std::function<void()> callback;
        bool enabled = true;
    };

    class MenuBar {
    public:
        void render();
        
        void set_panel_visibility_callback(const std::string& panel_name,  PanelVisibilityCallback callback);

        [[nodiscard]] bool is_panel_visible(const std::string& panel_name) const;

        void set_fps(float fps) { m_fps = fps; }

    private:
        void render_file_menu();
        void render_edit_menu();
        void render_view_menu();
        void render_gameobject_menu();
        void render_help_menu();

        std::map<std::string, bool> m_panel_visibility;
        std::map<std::string, PanelVisibilityCallback> m_panel_callbacks;
        float m_fps = 0.0f;
    };

}

