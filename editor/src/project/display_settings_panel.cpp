#include "project/display_settings_panel.h"

#include "ui/text.h"

#include <imgui.h>
#include <utility>

namespace CometEditor {
    void DisplaySettingsPanel::request(Comet::DisplaySettings current) {
        if(m_open)
            return;
        m_draft = current;
        m_request.reset();
        m_error.clear();
        m_open = true;
    }

    void DisplaySettingsPanel::close() {
        m_open = false;
        m_request.reset();
        m_error.clear();
    }

    void DisplaySettingsPanel::render() {
        if(!m_open)
            return;
        ImGui::SetNextWindowSize({480, 0}, ImGuiCond_FirstUseEver);
        if(ImGui::Begin(Ui::label("Project Settings - Display").c_str(), &m_open)) {
            ImGui::TextWrapped(
                "%s", Ui::text("Project defaults; existing players keep their saved choices."));
            ImGui::TextWrapped(
                "%s", Ui::text("Window size uses logical units; fullscreen uses monitor size."));
            ImGui::SetNextItemWidth(160);
            ImGui::InputInt(Ui::label("Window Width").c_str(), &m_draft.width, 0, 0);
            ImGui::SetNextItemWidth(160);
            ImGui::InputInt(Ui::label("Window Height").c_str(), &m_draft.height, 0, 0);
            const auto mode_label = [](Comet::WindowMode mode) {
                switch(mode) {
                    case Comet::WindowMode::Windowed:
                        return Ui::text("Windowed");
                    case Comet::WindowMode::Borderless:
                        return Ui::text("Borderless");
                    case Comet::WindowMode::Fullscreen:
                        return Ui::text("Fullscreen");
                }
                return "";
            };
            ImGui::SetNextItemWidth(160);
            if(ImGui::BeginCombo(Ui::label("Window Mode").c_str(), mode_label(m_draft.mode))) {
                for(const auto mode : {Comet::WindowMode::Windowed, Comet::WindowMode::Borderless,
                        Comet::WindowMode::Fullscreen}) {
                    if(ImGui::Selectable(mode_label(mode), m_draft.mode == mode))
                        m_draft.mode = mode;
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox("VSync", &m_draft.vsync);
            if(!m_error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.25f, 0.2f, 1.0f));
                ImGui::TextWrapped("%s", m_error.c_str());
                ImGui::PopStyleColor();
            }
            if(ImGui::Button(Ui::label("Save").c_str())) {
                if(auto valid = m_draft.validate(); valid)
                    m_request = m_draft;
                else
                    m_error = valid.error();
            }
            ImGui::SameLine();
            if(ImGui::Button(Ui::label("Cancel").c_str()))
                close();
        }
        ImGui::End();
        if(!m_open)
            close();
    }

    std::optional<Comet::DisplaySettings> DisplaySettingsPanel::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    void DisplaySettingsPanel::complete(const Comet::Result<void>& result) {
        if(result)
            close();
        else
            m_error = result.error();
    }
}
