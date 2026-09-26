#include "project/project_name_dialog.h"

#include "ui/language.h"

#include <imgui.h>

namespace CometEditor {
    void ProjectNameDialog::request(std::string current_name) {
        m_name = std::move(current_name);
        m_error.clear();
        m_request.reset();
        m_active = true;
        m_open_requested = true;
        m_close_requested = false;
    }

    void ProjectNameDialog::render() {
        if(!m_active)
            return;
        constexpr const char* title = "Rename Project";
        if(m_open_requested) {
            ImGui::OpenPopup(title);
            m_open_requested = false;
        }
        if(!ImGui::BeginPopupModal(
               Ui::label(title).c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if(m_close_requested) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            m_active = false;
            m_close_requested = false;
            return;
        }

        ImGui::SetNextItemWidth(360.0f);
        const bool submitted = ImGui::InputText(Ui::label("Name").c_str(), m_name.data(),
            m_name.capacity() + 1,
            ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
            [](ImGuiInputTextCallbackData* data) {
                auto& name = *static_cast<std::string*>(data->UserData);
                name.resize(static_cast<std::size_t>(data->BufTextLen));
                data->Buf = name.data();
                return 0;
            },
            &m_name);
        if(ImGui::Button(Ui::label("Rename").c_str(), ImVec2(100.0f, 0.0f)) || submitted)
            m_request = m_name;
        ImGui::SameLine();
        if(ImGui::Button(Ui::label("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            m_active = false;
            m_request.reset();
            m_error.clear();
        }
        if(!m_error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.25f, 0.2f, 1.0f));
            ImGui::TextWrapped("%s", m_error.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }

    void ProjectNameDialog::complete(const Comet::Result<void>& result) {
        m_close_requested = static_cast<bool>(result);
        m_error = result ? std::string{} : result.error();
    }
}
