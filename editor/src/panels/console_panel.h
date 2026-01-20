#pragma once
#include "editor_panel.h"
#include "common/logger.h"

namespace CometEditor {

    struct LogInfo {
        Comet::LogLevel level;
        std::string message;
    };

    class ConsolePanel : public EditorPanel {
    public:
        ConsolePanel();

        void render() override;

        void add_log(Comet::LogLevel level, const std::string& message);
        void clear_logs();

    private:
        std::vector<LogInfo> m_logs;
        bool m_show_trace = false;
        bool m_show_debug = false;
        bool m_show_info = true;
        bool m_show_warning = true;
        bool m_show_error = true;
        bool m_show_critical = true;
        static constexpr size_t MAX_LOGS = 10000;  // 限制日志数量，避免内存溢出
    };

}

