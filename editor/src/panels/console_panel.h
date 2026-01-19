#pragma once
#include "editor_panel.h"

namespace CometEditor {

    struct LogInfo {
        enum class Level {
            Info,
            Warning,
            Error
        };
        
        Level level;
        std::string message;
    };

    class ConsolePanel : public EditorPanel {
    public:
        ConsolePanel();
        
        void render() override;
        
        void add_log(LogInfo::Level level, const std::string& message);
        void clear_logs();

    private:
        std::vector<LogInfo> m_logs;
        bool m_show_info = true;
        bool m_show_warning = true;
        bool m_show_error = true;
    };

}

