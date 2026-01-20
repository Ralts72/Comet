#include "console_panel.h"
#include "common/logger.h"
#include <imgui.h>

namespace CometEditor {

    ConsolePanel::ConsolePanel()
        : EditorPanel("Log") {
    }

    void ConsolePanel::render() {
        if (!m_user_visible) return;

        ImGui::Begin(m_name.c_str(), &m_user_visible);

        // 工具栏
        if (ImGui::Button("Clear")) {
            clear_logs();
        }
        ImGui::SameLine();

        // 日志类型过滤
        ImGui::Checkbox("Trace", &m_show_trace);
        ImGui::SameLine();
        ImGui::Checkbox("Debug", &m_show_debug);
        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_show_info);
        ImGui::SameLine();
        ImGui::Checkbox("Warning", &m_show_warning);
        ImGui::SameLine();
        ImGui::Checkbox("Error", &m_show_error);
        ImGui::SameLine();
        ImGui::Checkbox("Critical", &m_show_critical);

        ImGui::Separator();

        // 日志列表
        ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        // 构建所有可见日志的文本
        static std::string log_buffer;
        log_buffer.clear();
        for (size_t i = 0; i < m_logs.size(); ++i) {
            const auto& log = m_logs[i];
            bool should_show = false;
            const char* level_str = nullptr;

            switch (log.level) {
                case Comet::LogLevel::Trace:
                    should_show = m_show_trace;
                    level_str = "[Trace]";
                    break;
                case Comet::LogLevel::Debug:
                    should_show = m_show_debug;
                    level_str = "[Debug]";
                    break;
                case Comet::LogLevel::Info:
                    should_show = m_show_info;
                    level_str = "[Info]";
                    break;
                case Comet::LogLevel::Warning:
                    should_show = m_show_warning;
                    level_str = "[Warning]";
                    break;
                case Comet::LogLevel::Error:
                    should_show = m_show_error;
                    level_str = "[Error]";
                    break;
                case Comet::LogLevel::Critical:
                    should_show = m_show_critical;
                    level_str = "[Critical]";
                    break;
            }

            if (should_show && level_str) {
                log_buffer += std::string(level_str) + " " + log.message + "\n";
            }
        }

        static ImGuiInputTextFlags flags = ImGuiInputTextFlags_ReadOnly;
        ImGui::InputTextMultiline("##LogContent",
            const_cast<char*>(log_buffer.c_str()),
            log_buffer.size() + 1,
            ImVec2(-1, -1),
            flags);

        // 自动滚动到底部
        static bool auto_scroll = true;
        if (auto_scroll && !log_buffer.empty()) {
            float scroll_y = ImGui::GetScrollY();
            float scroll_max_y = ImGui::GetScrollMaxY();
            if (scroll_y >= scroll_max_y - 10.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
        }

        ImGui::EndChild();

        ImGui::End();
    }

    void ConsolePanel::add_log(const Comet::LogLevel level, const std::string& message) {
        m_logs.push_back({level, message});

        if (m_logs.size() > MAX_LOGS) {
            m_logs.erase(m_logs.begin(), m_logs.begin() + (m_logs.size() - MAX_LOGS));
        }
    }

    void ConsolePanel::clear_logs() {
        m_logs.clear();
    }

}

