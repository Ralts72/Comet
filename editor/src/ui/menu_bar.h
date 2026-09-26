#pragma once

#include "editor_state.h"
#include "scene/command_history.h"
#include "ui/shortcuts.h"
#include "ui/language.h"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace CometEditor {

    class EditorPanel;

    class MenuBar {
    public:
        enum class Command {
            OpenProject,
            RenameProject,
            NewScene,
            OpenScene,
            SaveScene,
            SetStartupScene,
            ClearStartupScene,
            Undo,
            Redo,
            CopyEntity,
            PasteEntity,
            DeleteSelection
        };

        MenuBar(const EditorState& state, const CommandHistory& history,
            const EditorShortcuts& shortcuts);

        void render(const std::filesystem::path& current_scene = {},
            const std::filesystem::path& startup_scene = {},
            std::span<const std::filesystem::path> recent_projects = {});
        void collect_shortcuts();
        [[nodiscard]] std::optional<Command> take_command();
        [[nodiscard]] std::optional<std::filesystem::path> take_project_path();
        [[nodiscard]] std::optional<Ui::Language> take_language_request();

        void register_panel(EditorPanel& panel);

        void set_fps(const float fps) { m_fps = fps; }

    private:
        void render_file_menu(
            const std::filesystem::path& current_scene, const std::filesystem::path& startup_scene,
            std::span<const std::filesystem::path> recent_projects);
        void render_edit_menu();
        void render_view_menu();

        const EditorState& m_state;
        const CommandHistory& m_history;
        const EditorShortcuts& m_shortcuts;
        std::vector<EditorPanel*> m_panels;
        std::optional<Command> m_requested_command;
        std::optional<std::filesystem::path> m_requested_project_path;
        std::optional<Ui::Language> m_requested_language;
        float m_fps = 0.0f;
    };

}
