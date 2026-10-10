#include "project/display_settings_panel.h"

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
        if(ImGui::Begin("项目设置 - 显示###Project Settings - Display", &m_open)) {
            ImGui::TextWrapped(
                "%s", "修改项目默认值；已有玩家选择保持不变，可在游戏菜单恢复默认。");
            ImGui::TextWrapped("%s", "窗口宽高使用逻辑单位，全屏使用显示器尺寸。");
            ImGui::SetNextItemWidth(160);
            ImGui::InputInt("窗口宽度###Window Width", &m_draft.width, 0, 0);
            ImGui::SetNextItemWidth(160);
            ImGui::InputInt("窗口高度###Window Height", &m_draft.height, 0, 0);
            const auto mode_label = [](Comet::WindowMode mode) {
                switch(mode) {
                    case Comet::WindowMode::Windowed:
                        return "窗口";
                    case Comet::WindowMode::Borderless:
                        return "无边框";
                    case Comet::WindowMode::Fullscreen:
                        return "全屏";
                }
                return "";
            };
            ImGui::SetNextItemWidth(160);
            if(ImGui::BeginCombo("窗口模式###Window Mode", mode_label(m_draft.mode))) {
                for(const auto mode : {Comet::WindowMode::Windowed, Comet::WindowMode::Borderless,
                        Comet::WindowMode::Fullscreen}) {
                    if(ImGui::Selectable(mode_label(mode), m_draft.mode == mode))
                        m_draft.mode = mode;
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox("VSync", &m_draft.vsync);
            ImGui::SetNextItemWidth(160);
            ImGui::InputInt("帧率上限###Frame Rate Limit", &m_draft.frame_rate_limit, 0, 0);
            ImGui::TextWrapped("%s", "0 表示无上限；VSync 可能进一步降低实际 FPS。");
            const auto output_label = [](Comet::OutputMode mode) {
                if(mode == Comet::OutputMode::Auto)
                    return "自动";
                return mode == Comet::OutputMode::Hdr ? "HDR" : "SDR";
            };
            ImGui::SetNextItemWidth(160);
            if(ImGui::BeginCombo("输出模式###Output Mode", output_label(m_draft.output.mode))) {
                for(const auto mode :
                    {Comet::OutputMode::Sdr, Comet::OutputMode::Hdr, Comet::OutputMode::Auto}) {
                    if(ImGui::Selectable(output_label(mode), m_draft.output.mode == mode))
                        m_draft.output.mode = mode;
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(160);
            ImGui::DragFloat("HDR 高光范围###HDR Headroom", &m_draft.output.hdr_headroom, 0.1f,
                1.0f, 16.0f, "%.2f");
            ImGui::SetNextItemWidth(160);
            ImGui::DragFloat("HDR 相对白色###HDR White Level", &m_draft.output.hdr_white_level,
                0.05f, 0.5f, 2.0f, "%.2f");
            ImGui::TextWrapped("%s", "HDR 使用相对校准，不代表固定 nit 值；Play 使用 SDR 预览。");
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
