#include "menu_bar.h"
#include <imgui.h>

namespace CometEditor {

    void MenuBar::render() {
        if (ImGui::BeginMainMenuBar()) {
            render_file_menu();
            render_edit_menu();
            render_view_menu();
            render_gameobject_menu();
            render_help_menu();

            float fps_text_width = ImGui::CalcTextSize("FPS: 999.9").x;
            ImGui::SameLine(ImGui::GetWindowWidth() - fps_text_width - ImGui::GetStyle().WindowPadding.x);
            ImGui::Text("FPS: %.1f", m_fps);

            ImGui::EndMainMenuBar();
        }
    }

    void MenuBar::render_file_menu() {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                // TODO: 实现新建场景
            }
            if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
                // TODO: 实现打开场景
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                // TODO: 实现保存场景
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                // TODO: 实现退出
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::render_edit_menu() {
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences", "Ctrl+,")) {}
            ImGui::EndMenu();
        }
    }

    void MenuBar::render_view_menu() {
        if (ImGui::BeginMenu("View")) {
            for (auto& [name, visible] : m_panel_visibility) {
                if (ImGui::MenuItem(name.c_str(), nullptr, visible)) {
                    visible = !visible;
                    if (m_panel_callbacks.contains(name)) {
                        m_panel_callbacks.at(name)(visible);
                    }
                }
            }
            ImGui::EndMenu();
        }
    }

    void MenuBar::render_gameobject_menu() {
        if (ImGui::BeginMenu("GameObject")) {
            if (ImGui::MenuItem("Create Empty", "Ctrl+Shift+N")) {}
            if (ImGui::MenuItem("3D Object", nullptr, false)) {}
            if (ImGui::MenuItem("Light", nullptr, false)) {}
            if (ImGui::MenuItem("Camera", nullptr, false)) {}
            ImGui::EndMenu();
        }
    }

    void MenuBar::render_help_menu() {
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {}
            ImGui::EndMenu();
        }
    }

    void MenuBar::set_panel_visibility_callback(const std::string& panel_name, PanelVisibilityCallback callback) {
        if (m_panel_visibility.contains(panel_name)) {
            m_panel_callbacks[panel_name] = callback;
        } else {
            m_panel_visibility[panel_name] = true;
            m_panel_callbacks[panel_name] = callback;
        }
    }

    bool MenuBar::is_panel_visible(const std::string& panel_name) const {
        if (m_panel_visibility.contains(panel_name)) {
            return m_panel_visibility.at(panel_name);
        }
        return false;
    }

}

