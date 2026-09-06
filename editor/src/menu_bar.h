#pragma once

#include "editor_state.h"
#include "command_history.h"

#include <functional>
#include <map>
#include <string>
#include <utility>

namespace CometEditor {

    using PanelVisibilityCallback = std::function<void(bool)>;

    class MenuBar {
    public:
        enum class Command { NewScene, OpenScene, SaveScene, Undo, Redo };

        MenuBar(const EditorState& state, const CommandHistory& history)
            : m_state(state), m_history(history) {}

        void render();
        void collect_shortcuts();
        [[nodiscard]] std::optional<Command> take_command();

        void set_panel_visibility_callback(
            const std::string& panel_name, PanelVisibilityCallback callback);

        [[nodiscard]] bool is_panel_visible(const std::string& panel_name) const;

        void set_fps(const float fps) { m_fps = fps; }

    private:
        void render_file_menu();
        void render_edit_menu();
        void render_view_menu();
        void render_gameobject_menu();
        void render_help_menu();

        const EditorState& m_state;
        const CommandHistory& m_history;
        std::map<std::string, bool> m_panel_visibility;
        std::map<std::string, PanelVisibilityCallback> m_panel_callbacks;
        std::optional<Command> m_requested_command;
        float m_fps = 0.0f;
    };

}
