#include "ui/menu_bar.h"
#include "ui/editor_panel.h"

#include <algorithm>
#include <imgui.h>
#include <optional>
#include <utility>

namespace CometEditor {

    MenuBar::MenuBar(
        const EditorState& state, const CommandHistory& history, const EditorShortcuts& shortcuts)
        : m_state(state), m_history(history), m_shortcuts(shortcuts) {}

    void MenuBar::render(
        const std::filesystem::path& current_scene, const std::filesystem::path& startup_scene,
        std::span<const std::filesystem::path> recent_projects) {
        if(ImGui::BeginMainMenuBar()) {
            render_file_menu(current_scene, startup_scene, recent_projects);
            render_edit_menu();
            render_view_menu();
            if(ImGui::BeginMenu(Ui::label("Language").c_str())) {
                if(ImGui::MenuItem(
                       "简体中文###Chinese", nullptr, Ui::language() == Ui::Language::Chinese))
                    m_requested_language = Ui::Language::Chinese;
                if(ImGui::MenuItem("English", nullptr, Ui::language() == Ui::Language::English))
                    m_requested_language = Ui::Language::English;
                ImGui::EndMenu();
            }

            float fps_text_width = ImGui::CalcTextSize("FPS: 999.9").x;
            ImGui::SameLine(
                ImGui::GetWindowWidth() - fps_text_width - ImGui::GetStyle().WindowPadding.x);
            ImGui::Text("FPS: %.1f", m_fps);

            ImGui::EndMainMenuBar();
        }
    }

    void MenuBar::render_file_menu(
        const std::filesystem::path& current_scene, const std::filesystem::path& startup_scene,
        std::span<const std::filesystem::path> recent_projects) {
        if(ImGui::BeginMenu(Ui::label("File").c_str(), m_state.mode == EditorMode::Edit)) {
            if(ImGui::MenuItem(Ui::label("Open Project").c_str())) {
                m_requested_command = Command::OpenProject;
                m_requested_project_path.reset();
            }
            if(!recent_projects.empty()
                && ImGui::BeginMenu(Ui::label("Recent Projects").c_str())) {
                for(const auto& path : recent_projects) {
                    if(ImGui::MenuItem(path.generic_string().c_str())) {
                        m_requested_command = Command::OpenProject;
                        m_requested_project_path = path;
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
            if(ImGui::MenuItem(Ui::label("New Scene").c_str(),
                   m_shortcuts.label(EditorShortcuts::Action::NewScene, mac).c_str())) {
                m_requested_command = Command::NewScene;
            }
            if(ImGui::MenuItem(Ui::label("Open Scene").c_str(),
                   m_shortcuts.label(EditorShortcuts::Action::OpenScene, mac).c_str())) {
                m_requested_command = Command::OpenScene;
            }
            if(ImGui::MenuItem(Ui::label("Save Scene").c_str(),
                   m_shortcuts.label(EditorShortcuts::Action::SaveScene, mac).c_str())) {
                m_requested_command = Command::SaveScene;
            }
            ImGui::Separator();
            if(ImGui::MenuItem(Ui::label("Set Current Scene as Startup").c_str(), nullptr,
                   !current_scene.empty() && current_scene == startup_scene,
                   !current_scene.empty())) {
                m_requested_command = Command::SetStartupScene;
            }
            if(ImGui::MenuItem(Ui::label("Clear Startup Scene").c_str(), nullptr, false,
                   !startup_scene.empty())) {
                m_requested_command = Command::ClearStartupScene;
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::render_edit_menu() {
        if(ImGui::BeginMenu(Ui::label("Edit").c_str(), m_state.mode == EditorMode::Edit)) {
            const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
            if(ImGui::MenuItem(Ui::label("Undo").c_str(),
                   m_shortcuts.label(EditorShortcuts::Action::Undo, mac).c_str(), false,
                   m_history.can_undo())) {
                m_requested_command = Command::Undo;
            }
            if(ImGui::MenuItem(Ui::label("Redo").c_str(),
                   m_shortcuts.label(EditorShortcuts::Action::Redo, mac).c_str(), false,
                   m_history.can_redo())) {
                m_requested_command = Command::Redo;
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::collect_shortcuts() {
        if(m_requested_command || m_state.mode != EditorMode::Edit || ImGui::GetIO().WantTextInput
            || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            return;
        using Action = EditorShortcuts::Action;
        constexpr std::pair<Action, Command> commands[]{{Action::NewScene, Command::NewScene},
            {Action::OpenScene, Command::OpenScene}, {Action::SaveScene, Command::SaveScene},
            {Action::Undo, Command::Undo}, {Action::Redo, Command::Redo},
            {Action::CopyEntity, Command::CopyEntity}, {Action::PasteEntity, Command::PasteEntity},
            {Action::DeleteSelection, Command::DeleteSelection}};
        for(const auto& [action, command] : commands) {
            const bool pressed = m_shortcuts.pressed(action, ImGuiInputFlags_RouteGlobal);
            if(!pressed || m_requested_command)
                continue;
            if(command == Command::Undo && !m_history.can_undo())
                continue;
            if(command == Command::Redo && !m_history.can_redo())
                continue;
            m_requested_command = command;
        }
    }

    std::optional<MenuBar::Command> MenuBar::take_command() {
        return std::exchange(m_requested_command, std::nullopt);
    }

    std::optional<std::filesystem::path> MenuBar::take_project_path() {
        return std::exchange(m_requested_project_path, std::nullopt);
    }

    std::optional<Ui::Language> MenuBar::take_language_request() {
        return std::exchange(m_requested_language, std::nullopt);
    }

    void MenuBar::render_view_menu() {
        if(ImGui::BeginMenu(Ui::label("View").c_str())) {
            for(auto* panel : m_panels) {
                if(ImGui::MenuItem(panel->window_label().c_str(), nullptr, panel->is_open())) {
                    panel->toggle_visible();
                }
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::register_panel(EditorPanel& panel) {
        if(std::ranges::find(m_panels, &panel) == m_panels.end())
            m_panels.push_back(&panel);
    }
}
