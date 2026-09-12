#include "ui/console.h"
#include "diagnostics/logger.h"
#include <imgui.h>

namespace CometEditor {
    ConsolePanel::ConsolePanel() : EditorPanel("Log") {}

    void ConsolePanel::render() {
        if(!m_user_visible)
            return;

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }

        if(ImGui::Button("Clear")) {
            clear_logs();
        }
        ImGui::SameLine();

        bool filters_changed = ImGui::Checkbox("Trace", &m_show_trace);
        ImGui::SameLine();
        filters_changed |= ImGui::Checkbox("Debug", &m_show_debug);
        ImGui::SameLine();
        filters_changed |= ImGui::Checkbox("Info", &m_show_info);
        ImGui::SameLine();
        filters_changed |= ImGui::Checkbox("Warning", &m_show_warning);
        ImGui::SameLine();
        filters_changed |= ImGui::Checkbox("Error", &m_show_error);
        ImGui::SameLine();
        filters_changed |= ImGui::Checkbox("Critical", &m_show_critical);

        ImGui::Separator();

        ImGui::BeginChild(
            "LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        update_log_buffer(filters_changed);
        ImGui::InputTextMultiline("##LogContent", m_log_buffer.data(),
            m_log_buffer.size() + 1, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);

        if(!m_log_buffer.empty()
            && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
        ImGui::End();
    }

    void ConsolePanel::update_log_buffer(const bool filters_changed) {
        std::lock_guard lock(m_logs_mutex);
        if(!m_logs_dirty && !filters_changed) {
            return;
        }
        m_log_buffer.clear();
        for(const auto& [level, message] : m_logs) {
            bool should_show = false;
            const char* level_str = nullptr;

            switch(level) {
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

            if(should_show && level_str) {
                m_log_buffer += std::string(level_str) + " " + message + "\n";
            }
        }

        m_logs_dirty = false;
    }

    void ConsolePanel::add_log(const Comet::LogLevel level, const std::string& message) {
        std::lock_guard lock(m_logs_mutex);
        m_logs.push_back({.level = level, .message = message});
        m_logs_dirty = true;

        if(m_logs.size() > MAX_LOGS) {
            m_logs.pop_front();
        }
    }

    void ConsolePanel::clear_logs() {
        std::lock_guard lock(m_logs_mutex);
        m_logs.clear();
        m_logs_dirty = true;
    }
}
