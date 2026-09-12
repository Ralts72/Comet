#include "ui/menu_bar.h"
#include "ui/editor_panel.h"

#include <algorithm>
#include <imgui.h>
#include <optional>
#include <utility>

namespace CometEditor {

    MenuBar::MenuBar(const EditorState& state, const CommandHistory& history,
        const EditorShortcuts& shortcuts)
        : m_state(state), m_history(history), m_shortcuts(shortcuts) {}

    void MenuBar::render() {
        if(ImGui::BeginMainMenuBar()) {
            render_file_menu();
            render_edit_menu();
            render_view_menu();

            float fps_text_width = ImGui::CalcTextSize("FPS: 999.9").x;
            ImGui::SameLine(ImGui::GetWindowWidth() - fps_text_width
                            - ImGui::GetStyle().WindowPadding.x);
            ImGui::Text("FPS: %.1f", m_fps);

            ImGui::EndMainMenuBar();
        }
    }

    void MenuBar::render_file_menu() {
        if(ImGui::BeginMenu("File", m_state.mode == EditorMode::Edit)) {
            const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
            if(ImGui::MenuItem("New Scene",
                   m_shortcuts.label(EditorShortcuts::Action::NewScene, mac).c_str())) {
                m_requested_command = Command::NewScene;
            }
            if(ImGui::MenuItem("Open Scene",
                   m_shortcuts.label(EditorShortcuts::Action::OpenScene, mac).c_str())) {
                m_requested_command = Command::OpenScene;
            }
            if(ImGui::MenuItem("Save Scene",
                   m_shortcuts.label(EditorShortcuts::Action::SaveScene, mac).c_str())) {
                m_requested_command = Command::SaveScene;
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::render_edit_menu() {
        if(ImGui::BeginMenu("Edit", m_state.mode == EditorMode::Edit)) {
            const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
            if(ImGui::MenuItem("Undo",
                   m_shortcuts.label(EditorShortcuts::Action::Undo, mac).c_str(), false,
                   m_history.can_undo())) {
                m_requested_command = Command::Undo;
            }
            if(ImGui::MenuItem("Redo",
                   m_shortcuts.label(EditorShortcuts::Action::Redo, mac).c_str(), false,
                   m_history.can_redo())) {
                m_requested_command = Command::Redo;
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::collect_shortcuts() {
        if(m_requested_command || m_state.mode != EditorMode::Edit
            || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()
            || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            return;
        using Action = EditorShortcuts::Action;
        constexpr std::pair<Action, Command> commands[]{
            {Action::NewScene, Command::NewScene},
            {Action::OpenScene, Command::OpenScene},
            {Action::SaveScene, Command::SaveScene}, {Action::Undo, Command::Undo},
            {Action::Redo, Command::Redo}};
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

    void MenuBar::render_view_menu() {
        if(ImGui::BeginMenu("View")) {
            for(auto* panel : m_panels) {
                if(ImGui::MenuItem(
                       panel->get_name().c_str(), nullptr, panel->is_open())) {
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
