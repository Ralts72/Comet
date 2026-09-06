#include "menu_bar.h"
#include <imgui.h>

namespace CometEditor {

    void MenuBar::render() {
        if(ImGui::BeginMainMenuBar()) {
            render_file_menu();
            render_edit_menu();
            render_view_menu();
            render_gameobject_menu();
            render_help_menu();

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
            if(ImGui::MenuItem("New Scene", mac ? "Cmd+N" : "Ctrl+N")) {
                m_requested_command = Command::NewScene;
            }
            if(ImGui::MenuItem("Open Scene", mac ? "Cmd+O" : "Ctrl+O")) {
                m_requested_command = Command::OpenScene;
            }
            if(ImGui::MenuItem("Save Scene", mac ? "Cmd+S" : "Ctrl+S")) {
                m_requested_command = Command::SaveScene;
            }
            ImGui::Separator();
            if(ImGui::MenuItem("Exit", "Alt+F4")) {
                // TODO: 实现退出
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::render_edit_menu() {
        if(ImGui::BeginMenu("Edit", m_state.mode == EditorMode::Edit)) {
            const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
            if(ImGui::MenuItem(
                   "Undo", mac ? "Cmd+Z" : "Ctrl+Z", false, m_history.can_undo())) {
                m_requested_command = Command::Undo;
            }
            if(ImGui::MenuItem(
                   "Redo", mac ? "Cmd+Shift+Z" : "Ctrl+Y", false, m_history.can_redo())) {
                m_requested_command = Command::Redo;
            }
            ImGui::Separator();
            if(ImGui::MenuItem("Preferences", "Ctrl+,")) {}
            ImGui::EndMenu();
        }
    }

    void MenuBar::collect_shortcuts() {
        if(m_requested_command || m_state.mode != EditorMode::Edit
            || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()
            || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            return;
        constexpr auto flags = ImGuiInputFlags_RouteGlobal;
        // ImGui 在 macOS 上自动把快捷键中的 Ctrl 映射为 Cmd。
        if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, flags)
            || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, flags)) {
            if(m_history.can_redo())
                m_requested_command = Command::Redo;
        } else if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, flags)) {
            if(m_history.can_undo())
                m_requested_command = Command::Undo;
        } else if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, flags)) {
            m_requested_command = Command::NewScene;
        } else if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, flags)) {
            m_requested_command = Command::OpenScene;
        } else if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, flags)) {
            m_requested_command = Command::SaveScene;
        }
    }

    std::optional<MenuBar::Command> MenuBar::take_command() {
        return std::exchange(m_requested_command, std::nullopt);
    }

    void MenuBar::render_view_menu() {
        if(ImGui::BeginMenu("View")) {
            for(auto& [name, visible] : m_panel_visibility) {
                if(ImGui::MenuItem(name.c_str(), nullptr, visible)) {
                    visible = !visible;
                    if(m_panel_callbacks.contains(name)) {
                        m_panel_callbacks.at(name)(visible);
                    }
                }
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::render_gameobject_menu() {
        if(ImGui::BeginMenu("GameObject")) {
            if(ImGui::MenuItem("Create Empty", "Ctrl+Shift+N")) {}
            if(ImGui::MenuItem("3D Object", nullptr, false)) {}
            if(ImGui::MenuItem("Light", nullptr, false)) {}
            if(ImGui::MenuItem("Camera", nullptr, false)) {}
            ImGui::EndMenu();
        }
    }

    void MenuBar::render_help_menu() {
        if(ImGui::BeginMenu("Help")) {
            if(ImGui::MenuItem("About")) {}
            ImGui::EndMenu();
        }
    }

    void MenuBar::set_panel_visibility_callback(const std::string& panel_name,
        PanelVisibilityCallback callback, const bool initially_visible) {
        if(m_panel_visibility.contains(panel_name)) {
            m_panel_callbacks[panel_name] = callback;
        } else {
            m_panel_visibility[panel_name] = initially_visible;
            m_panel_callbacks[panel_name] = callback;
        }
    }

    bool MenuBar::is_panel_visible(const std::string& panel_name) const {
        if(m_panel_visibility.contains(panel_name)) {
            return m_panel_visibility.at(panel_name);
        }
        return false;
    }

}
