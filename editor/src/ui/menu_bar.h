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
            ProjectDisplaySettings,
            ProjectQualitySettings,
            ProjectAudioSettings,
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
        void set_undo_state(bool can_undo, bool can_redo) {
            m_undo_state = std::pair{can_undo, can_redo};
        }
        [[nodiscard]] std::optional<Request> take_request();

        void register_panel(EditorPanel& panel);

        void set_fps(const float fps) { m_fps = fps; }

    private:
        void render_file_menu(std::span<const std::filesystem::path> recent_projects);
        void render_project_menu(
            const std::filesystem::path& current_scene, const std::filesystem::path& startup_scene);
        void render_edit_menu();
        void render_view_menu();
        [[nodiscard]] bool can_undo() const {
            return m_undo_state ? m_undo_state->first : m_history.can_undo();
        }
        [[nodiscard]] bool can_redo() const {
            return m_undo_state ? m_undo_state->second : m_history.can_redo();
        }

        const EditorState& m_state;
        const CommandHistory& m_history;
        const EditorShortcuts& m_shortcuts;
        std::vector<EditorPanel*> m_panels;
        std::vector<std::filesystem::path> m_available_scenes;
        std::optional<Request> m_request;
        std::optional<std::pair<bool, bool>> m_undo_state;
        float m_fps = 0.0f;
    };

}
