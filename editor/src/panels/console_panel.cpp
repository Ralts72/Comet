#include "console_panel.h"
#include <imgui.h>

namespace CometEditor {

    ConsolePanel::ConsolePanel() 
        : EditorPanel("Log") {
        // 示例日志
        add_log(LogInfo::Level::Info, "Editor initialized");
        add_log(LogInfo::Level::Info, "Scene loaded successfully");
        add_log(LogInfo::Level::Warning, "No material assigned to object");
        add_log(LogInfo::Level::Error, "Failed to load texture");
    }

    void ConsolePanel::render() {
        if (!m_visible) return;
        
        ImGui::Begin(m_name.c_str(), &m_visible);
        
        // 工具栏
        if (ImGui::Button("Clear")) {
            clear_logs();
        }
        ImGui::SameLine();
        
        // 日志类型过滤
        ImGui::Checkbox("Info", &m_show_info);
        ImGui::SameLine();
        ImGui::Checkbox("Warning", &m_show_warning);
        ImGui::SameLine();
        ImGui::Checkbox("Error", &m_show_error);
        
        ImGui::Separator();
        
        // 日志列表
        ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        
        for (const auto& log : m_logs) {
            bool should_show = false;
            ImVec4 color;
            
            switch (log.level) {
                case LogInfo::Level::Info:
                    should_show = m_show_info;
                    color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                    break;
                case LogInfo::Level::Warning:
                    should_show = m_show_warning;
                    color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
                    break;
                case LogInfo::Level::Error:
                    should_show = m_show_error;
                    color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                    break;
            }
            
            if (should_show) {
                const char* level_str = log.level == LogInfo::Level::Info ? "[Info]" :
                                       log.level == LogInfo::Level::Warning ? "[Warning]" : "[Error]";
                ImGui::TextColored(color, "%s %s", level_str, log.message.c_str());
            }
        }
        
        ImGui::EndChild();
        
        ImGui::End();
    }

    void ConsolePanel::add_log(const LogInfo::Level level, const std::string& message) {
        m_logs.push_back({level, message});
    }

    void ConsolePanel::clear_logs() {
        m_logs.clear();
    }

}

