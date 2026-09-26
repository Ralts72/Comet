#include "ui/path_dialog.h"
#include "ui/language.h"
#include <algorithm>
#include <utility>
#include <imgui.h>
namespace CometEditor {
    void PathDialog::request(const Action dialog, const std::filesystem::path& current_path,
        const std::filesystem::path& default_directory) {
        m_action = dialog;
        m_open_requested = true;
        m_close_requested = false;
        m_cancelled = false;
        m_request.reset();
        m_error.clear();

        std::string initial_path = current_path.string();
        if(dialog == Action::SaveScene && initial_path.empty()) {
            initial_path = (default_directory / "untitled.scene").string();
        } else if(initial_path.empty()) {
            initial_path = default_directory.string() + "/";
        }
        m_path_buffer.fill('\0');
        std::copy_n(initial_path.data(), std::min(initial_path.size(), m_path_buffer.size() - 1),
            m_path_buffer.data());
    }

    void PathDialog::render() {
        if(m_action == Action::None) {
            return;
        }

        const bool is_open = m_action != Action::SaveScene;
        const char* title = "Open Scene";
        if(m_action == Action::SaveScene)
            title = "Save Scene";
        else if(m_action == Action::OpenProject)
            title = "Open Project";
        if(m_open_requested) {
            ImGui::OpenPopup(title);
            m_open_requested = false;
        }

        if(!ImGui::BeginPopupModal(
               Ui::label(title).c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        if(m_close_requested) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            m_action = Action::None;
            m_close_requested = false;
            return;
        }

        ImGui::SetNextItemWidth(560.0f);
        const bool submitted = ImGui::InputText(Ui::label("Path").c_str(), m_path_buffer.data(),
            m_path_buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);

        const char* action = is_open ? "Open" : "Save";
        if((ImGui::Button(Ui::label(action).c_str(), ImVec2(100.0f, 0.0f)) || submitted)) {
            m_request = Request{m_action, m_path_buffer.data()};
        }
        ImGui::SameLine();
        if(ImGui::Button(Ui::label("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            m_cancelled = true;
            ImGui::CloseCurrentPopup();
            m_action = Action::None;
            m_request.reset();
            m_error.clear();
        }

        if(!m_error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.25f, 0.2f, 1.0f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560.0f);
            ImGui::TextWrapped("%s", m_error.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }

    std::optional<PathDialog::Request> PathDialog::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    void PathDialog::complete(const Comet::Result<void, Comet::Error>& result) {
        m_close_requested = static_cast<bool>(result);
        m_error = result ? std::string{} : result.error().message;
    }

}
