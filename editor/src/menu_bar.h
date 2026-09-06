#pragma once

#include "editor_state.h"
#include "command_history.h"

#include <functional>
#include <map>
#include <string>
#include <utility>

namespace CometEditor {

    class EditorPanel;

    class MenuBar {
    public:
        enum class Command { NewScene, OpenScene, SaveScene, Undo, Redo };

        MenuBar(const EditorState& state, const CommandHistory& history)
            : m_state(state), m_history(history) {}

        void render();
        void collect_shortcuts();
        [[nodiscard]] std::optional<Command> take_command();

        // 仅观察现有面板；调用方保证面板比菜单活得更久。
        void add_panel(EditorPanel& panel);

        void set_fps(const float fps) { m_fps = fps; }

    private:
        void render_file_menu();
        void render_edit_menu();
        void render_view_menu();
        void render_gameobject_menu();
        void render_help_menu();

        const EditorState& m_state;
        const CommandHistory& m_history;
        std::map<std::string, std::reference_wrapper<EditorPanel>> m_panels;
        std::optional<Command> m_requested_command;
        float m_fps = 0.0f;
    };

}
