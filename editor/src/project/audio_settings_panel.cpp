#include "project/audio_settings_panel.h"

#include "ui/text.h"

#include <imgui.h>
#include <utility>

namespace CometEditor {
    void AudioSettingsPanel::request(Comet::AudioSettings current) {
        if(m_open)
            return;
        m_draft = current;
        m_request.reset();
        m_error.clear();
        m_open = true;
    }

    void AudioSettingsPanel::close() {
        m_open = false;
        m_request.reset();
        m_error.clear();
    }

    void AudioSettingsPanel::render() {
        if(!m_open)
            return;
        ImGui::SetNextWindowSize({480, 0}, ImGuiCond_FirstUseEver);
        if(ImGui::Begin(Ui::label("Project Settings - Audio").c_str(), &m_open)) {
            ImGui::TextWrapped(
                "%s", Ui::text("Project defaults; existing players keep their saved choices."));
            for(const auto& [label, volume] : {std::pair{"Master Volume", &m_draft.master_volume},
                    std::pair{"Sound Effects", &m_draft.effects_volume},
                    std::pair{"Music", &m_draft.music_volume}}) {
                ImGui::SetNextItemWidth(220);
                ImGui::SliderFloat(Ui::label(label).c_str(), volume, 0, 1, "%.2f");
            }
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

    std::optional<Comet::AudioSettings> AudioSettingsPanel::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    void AudioSettingsPanel::complete(const Comet::Result<void>& result) {
        if(result)
            close();
        else
            m_error = result.error();
    }
}
