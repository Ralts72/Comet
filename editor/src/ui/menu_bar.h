#pragma once

#include "editor_state.h"
#include "scene/command_history.h"
#include "ui/shortcuts.h"

#include <optional>
#include <vector>

namespace CometEditor {

    class EditorPanel;

    class MenuBar {
    public:
        enum class Command { NewScene, OpenScene, SaveScene, Undo, Redo };

        MenuBar(const EditorState& state, const CommandHistory& history,
            const EditorShortcuts& shortcuts);

        void render();
        void collect_shortcuts();
        [[nodiscard]] std::optional<Command> take_command();

        void register_panel(EditorPanel& panel);

        void set_fps(const float fps) { m_fps = fps; }

    private:
        void render_file_menu();
        void render_edit_menu();
        void render_view_menu();

        const EditorState& m_state;
        const CommandHistory& m_history;
        const EditorShortcuts& m_shortcuts;
        std::vector<EditorPanel*> m_panels;
        std::optional<Command> m_requested_command;
        float m_fps = 0.0f;
    };

}
