#include "ui/menu_bar.h"
#include "ui/editor_panel.h"
#include "ui/text.h"

#include <algorithm>
#include <imgui.h>
#include <optional>
#include <utility>

namespace CometEditor {

    MenuBar::MenuBar(
        const EditorState& state, const CommandHistory& history, const EditorShortcuts& shortcuts)
        : m_state(state), m_history(history), m_shortcuts(shortcuts) {}

    void MenuBar::render(const std::filesystem::path& current_scene,
        const std::filesystem::path& startup_scene,
        std::span<const std::filesystem::path> recent_projects) {
        if(ImGui::BeginMainMenuBar()) {
            render_file_menu(recent_projects);
            render_project_menu(current_scene, startup_scene);
            render_edit_menu();
            render_view_menu();
            float fps_text_width = ImGui::CalcTextSize("FPS: 999.9").x;
            ImGui::SameLine(
                ImGui::GetWindowWidth() - fps_text_width - ImGui::GetStyle().WindowPadding.x);
            ImGui::Text("FPS: %.1f", m_fps);

            ImGui::EndMainMenuBar();
        }
    }

    void MenuBar::render_file_menu(std::span<const std::filesystem::path> recent_projects) {
        if(ImGui::BeginMenu(Ui::label("File").c_str(), m_state.mode == EditorMode::Edit)) {
            if(ImGui::MenuItem(Ui::label("New Project").c_str()))
                m_request = Request{Command::NewProject, {}};
            if(ImGui::MenuItem(Ui::label("Open Project").c_str())) {
                m_request = Request{Command::OpenProject, {}};
            }
            if(!recent_projects.empty() && ImGui::BeginMenu(Ui::label("Recent Projects").c_str())) {
                for(const auto& path : recent_projects) {
                    if(ImGui::MenuItem(path.generic_string().c_str())) {
                        m_request = Request{Command::OpenProject, path};
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
            if(ImGui::MenuItem(Ui::label("New Scene").c_str(),
                   m_shortcuts.label(EditorShortcuts::Action::NewScene, mac).c_str())) {
                m_request = Request{Command::NewScene, {}};
            }
            if(ImGui::MenuItem(Ui::label("Open Scene").c_str(),
                   m_shortcuts.label(EditorShortcuts::Action::OpenScene, mac).c_str())) {
                m_request = Request{Command::OpenScene, {}};
            }
            if(ImGui::MenuItem(Ui::label("Save Scene").c_str(),
                   m_shortcuts.label(EditorShortcuts::Action::SaveScene, mac).c_str())) {
                m_request = Request{Command::SaveScene, {}};
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::render_project_menu(
        const std::filesystem::path& current_scene, const std::filesystem::path& startup_scene) {
        if(ImGui::BeginMenu(Ui::label("Project").c_str(), m_state.mode == EditorMode::Edit)) {
            if(ImGui::MenuItem(Ui::label("Rename Project...").c_str()))
                m_request = Request{Command::RenameProject, {}};
            if(ImGui::BeginMenu(Ui::label("Startup Scene").c_str())) {
                for(const auto& path : m_available_scenes) {
                    if(ImGui::MenuItem(
                           path.generic_string().c_str(), nullptr, path == startup_scene)) {
                        m_request = Request{Command::SetStartupScene, path};
                    }
                }
                if(!current_scene.empty()
                    && std::ranges::find(m_available_scenes, current_scene)
                           == m_available_scenes.end()) {
                    if(ImGui::MenuItem(current_scene.generic_string().c_str(), nullptr,
                           current_scene == startup_scene)) {
                        m_request = Request{Command::SetStartupScene, current_scene};
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if(ImGui::BeginMenu(Ui::label("Settings").c_str())) {
                if(ImGui::MenuItem(Ui::label("Display").c_str()))
                    m_request = Request{Command::ProjectDisplaySettings, {}};
                if(ImGui::MenuItem(Ui::label("Quality").c_str()))
                    m_request = Request{Command::ProjectQualitySettings, {}};
                if(ImGui::MenuItem(Ui::label("Audio").c_str()))
                    m_request = Request{Command::ProjectAudioSettings, {}};
                if(ImGui::MenuItem(Ui::label("Input").c_str()))
                    m_request = Request{Command::ProjectInputSettings, {}};
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::render_edit_menu() {
        if(ImGui::BeginMenu(Ui::label("Edit").c_str(), m_state.mode == EditorMode::Edit)) {
            const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
            if(ImGui::MenuItem(Ui::label("Undo").c_str(),
                   m_shortcuts.label(EditorShortcuts::Action::Undo, mac).c_str(), false,
                   can_undo())) {
                m_request = Request{Command::Undo, {}};
            }
            if(ImGui::MenuItem(Ui::label("Redo").c_str(),
                   m_shortcuts.label(EditorShortcuts::Action::Redo, mac).c_str(), false,
                   can_redo())) {
                m_request = Request{Command::Redo, {}};
            }
            ImGui::Separator();
            if(ImGui::MenuItem(Ui::label("Keyboard Shortcuts...").c_str()))
                m_request = Request{Command::KeyboardShortcuts, {}};
            ImGui::EndMenu();
        }
    }

    void MenuBar::collect_shortcuts() {
        if(m_request || m_state.mode != EditorMode::Edit || ImGui::GetIO().WantTextInput
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
            if(!pressed || m_request)
                continue;
            if(command == Command::Undo && !can_undo())
                continue;
            if(command == Command::Redo && !can_redo())
                continue;
            m_request = Request{command, {}};
        }
    }

    std::optional<MenuBar::Request> MenuBar::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    void MenuBar::set_available_scenes(std::vector<std::filesystem::path> scenes) {
        m_available_scenes = std::move(scenes);
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
