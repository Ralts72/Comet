#include "scene_file_dialog.h"
#include "scene_document.h"
#include <algorithm>
#include <imgui.h>
namespace CometEditor {
    void SceneFileDialog::request(const Action dialog, SceneDocument& document,
        const std::filesystem::path& scene_directory) {
        m_action = dialog;
        m_open_requested = true;
        document.clear_error();

        std::string initial_path = document.get_path();
        if(dialog == Action::Save && initial_path.empty()) {
            initial_path = (scene_directory / "untitled.scene").string();
        } else if(dialog == Action::Open && initial_path.empty()) {
            initial_path = scene_directory.string() + "/";
        }
        m_path_buffer.fill('\0');
        std::copy_n(initial_path.data(),
            std::min(initial_path.size(), m_path_buffer.size() - 1),
            m_path_buffer.data());
    }

    bool SceneFileDialog::render(SceneDocument& document) {
        if(m_action == Action::None) {
            return false;
        }

        const bool is_open = m_action == Action::Open;
        const char* title = is_open ? "Open Scene" : "Save Scene";
        if(m_open_requested) {
            ImGui::OpenPopup(title);
            m_open_requested = false;
        }

        bool opened = false;
        if(!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return false;
        }

        ImGui::SetNextItemWidth(560.0f);
        const bool submitted = ImGui::InputText("Path", m_path_buffer.data(),
            m_path_buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);

        const char* action = is_open ? "Open" : "Save";
        if((ImGui::Button(action, ImVec2(100.0f, 0.0f)) || submitted)) {
            const std::string path(m_path_buffer.data());
            const bool succeeded = is_open ? document.open(path) : document.save(path);
            if(succeeded) {
                if(is_open) {
                    opened = true;
                }
                ImGui::CloseCurrentPopup();
                m_action = Action::None;
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            m_action = Action::None;
            document.clear_error();
        }

        if(!document.get_last_error().empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.25f, 0.2f, 1.0f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560.0f);
            ImGui::TextWrapped("%s", document.get_last_error().c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
        return opened;
    }

}
