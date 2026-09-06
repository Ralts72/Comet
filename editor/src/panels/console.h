#pragma once
#include "editor_panel.h"
#include "diagnostics/logger.h"
#include <deque>
#include <mutex>

namespace CometEditor {

    struct LogInfo {
        Comet::LogLevel level;
        std::string message;
    };

    class ConsolePanel: public EditorPanel {
    public:
        ConsolePanel();

        void render() override;

        void add_log(Comet::LogLevel level, const std::string& message);
        void clear_logs();

    private:
        void update_log_buffer(bool filters_changed);

        std::mutex m_logs_mutex;
        std::deque<LogInfo> m_logs;
        bool m_logs_dirty = true;
        std::string m_log_buffer;
        bool m_show_trace = false;
        bool m_show_debug = false;
        bool m_show_info = true;
        bool m_show_warning = true;
        bool m_show_error = true;
        bool m_show_critical = true;
        static constexpr size_t MAX_LOGS = 10000;
    };

}
