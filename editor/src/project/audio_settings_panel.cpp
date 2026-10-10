#include "project/audio_settings_panel.h"

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
        if(ImGui::Begin("项目设置 - 音频###Project Settings - Audio", &m_open)) {
            ImGui::TextWrapped(
                "%s", "修改项目默认值；已有玩家选择保持不变，可在游戏菜单恢复默认。");
            for(const auto& [label, volume] :
                {std::pair{"主音量###Master Volume", &m_draft.master_volume},
                    std::pair{"音效###Sound Effects", &m_draft.effects_volume},
                    std::pair{"音乐###Music", &m_draft.music_volume}}) {
                ImGui::SetNextItemWidth(220);
                ImGui::SliderFloat(label, volume, 0, 1, "%.2f");
            }
            if(!m_error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.25f, 0.2f, 1.0f));
                ImGui::TextWrapped("%s", m_error.c_str());
                ImGui::PopStyleColor();
            }
            if(ImGui::Button("保存###Save")) {
                if(auto valid = m_draft.validate(); valid)
                    m_request = m_draft;
                else
                    m_error = valid.error();
            }
            ImGui::SameLine();
            if(ImGui::Button("取消###Cancel"))
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
