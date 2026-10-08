#pragma once

#include "editor_state.h"
#include "scene/command_history.h"
#include "ui/shortcuts.h"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace CometEditor {

    class EditorPanel;

    class MenuBar {
    public:
        enum class Command {
            NewProject,
            OpenProject,
            RenameProject,
            ProjectInputSettings,
            NewScene,
            OpenScene,
            SaveScene,
            SetStartupScene,
            Undo,
            Redo,
            KeyboardShortcuts,
            CopyEntity,
            PasteEntity,
            DeleteSelection
        };
        struct Request {
            Command command;
            std::filesystem::path path;

            bool operator==(const Request&) const = default;
        };

        MenuBar(const EditorState& state, const CommandHistory& history,
            const EditorShortcuts& shortcuts);

        void render(const std::filesystem::path& current_scene = {},
            const std::filesystem::path& startup_scene = {},
            std::span<const std::filesystem::path> recent_projects = {});
        void set_available_scenes(std::vector<std::filesystem::path> scenes);
        void collect_shortcuts();
        [[nodiscard]] std::optional<Request> take_request();

        void register_panel(EditorPanel& panel);

        void set_fps(const float fps) { m_fps = fps; }

    private:
        void render_file_menu(std::span<const std::filesystem::path> recent_projects);
        void render_project_menu(
            const std::filesystem::path& current_scene, const std::filesystem::path& startup_scene);
        void render_edit_menu();
        void render_view_menu();

        const EditorState& m_state;
        const CommandHistory& m_history;
        const EditorShortcuts& m_shortcuts;
        std::vector<EditorPanel*> m_panels;
        std::vector<std::filesystem::path> m_available_scenes;
        std::optional<Request> m_request;
        float m_fps = 0.0f;
    };

}
