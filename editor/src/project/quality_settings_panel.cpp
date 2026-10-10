#include "project/quality_settings_panel.h"

#include "ui/text.h"

#include <imgui.h>
#include <utility>

namespace CometEditor {
    void QualitySettingsPanel::request(Comet::QualitySettings current) {
        if(m_open)
            return;
        m_draft = current;
        m_request.reset();
        m_error.clear();
        m_open = true;
    }

    void QualitySettingsPanel::close() {
        m_open = false;
        m_request.reset();
        m_error.clear();
    }

    void QualitySettingsPanel::render() {
        if(!m_open)
            return;
        ImGui::SetNextWindowSize({480, 0}, ImGuiCond_FirstUseEver);
        if(ImGui::Begin(Ui::label("Project Settings - Quality").c_str(), &m_open)) {
            ImGui::TextWrapped(
                "%s", Ui::text("Project defaults; existing players keep their saved choices."));
            ImGui::SetNextItemWidth(160);
            const auto samples = std::to_string(m_draft.msaa_samples) + "x";
            if(ImGui::BeginCombo("MSAA", samples.c_str())) {
                for(const uint32_t count : {1u, 2u, 4u, 8u}) {
                    const auto label = std::to_string(count) + "x";
                    if(ImGui::Selectable(label.c_str(), m_draft.msaa_samples == count))
                        m_draft.msaa_samples = count;
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(220);
            ImGui::SliderFloat(
                Ui::label("Anisotropy").c_str(), &m_draft.max_anisotropy, 1, 16, "%.0fx");
            ImGui::SetNextItemWidth(220);
            ImGui::SliderFloat(
                Ui::label("Render Scale").c_str(), &m_draft.render_scale, 0.5f, 1, "%.2f");
            ImGui::TextWrapped(
                "%s", Ui::text("Render scale affects the scene; UI keeps output resolution."));
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

    std::optional<Comet::QualitySettings> QualitySettingsPanel::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    void QualitySettingsPanel::complete(const Comet::Result<void>& result) {
        if(result)
            close();
        else
            m_error = result.error();
    }
}
