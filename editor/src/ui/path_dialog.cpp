#include "ui/path_dialog.h"
#include "ui/widgets.h"
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

        m_path = current_path.string();
        if(dialog == Action::SaveScene && m_path.empty()) {
            m_path = (default_directory / "untitled.scene").string();
        } else if(dialog == Action::CreateProject && m_path.empty()) {
            m_path = (default_directory / "NewProject").string();
        } else if(m_path.empty()) {
            m_path = default_directory.string() + "/";
        }
    }

    void PathDialog::render() {
        if(m_action == Action::None) {
            return;
        }

        const bool is_open = m_action != Action::SaveScene;
        const char* title = "打开场景###Open Scene";
        if(m_action == Action::SaveScene)
            title = "保存场景###Save Scene";
        else if(m_action == Action::OpenProject)
            title = "打开项目###Open Project";
        else if(m_action == Action::CreateProject)
            title = "新建项目###New Project";
        if(m_open_requested) {
            ImGui::OpenPopup(title);
            m_open_requested = false;
        }

        if(!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
        const bool submitted =
            Ui::input_text("路径###Path", m_path, ImGuiInputTextFlags_EnterReturnsTrue);

        const char* action = is_open ? "打开###Open" : "保存###Save";
        if(m_action == Action::CreateProject)
            action = "Create";
        if((ImGui::Button(action, ImVec2(100.0f, 0.0f)) || submitted)) {
            m_request = Request{m_action, m_path};
        }
        ImGui::SameLine();
        if(ImGui::Button("取消###Cancel", ImVec2(100.0f, 0.0f))) {
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
