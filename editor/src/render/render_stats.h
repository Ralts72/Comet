#pragma once

#include "ui/editor_panel.h"
#include "diagnostics/timing_history.h"
#include "graphics/resource/memory_budget.h"
#include <optional>

namespace Comet {
    class Engine;
}

namespace CometEditor {
    class RenderStatsPanel final: public EditorPanel {
    public:
        explicit RenderStatsPanel(const Comet::Engine& engine);
        void render() override;
        [[nodiscard]] std::optional<bool> take_capture_request();
        [[nodiscard]] bool take_allocation_report_request();

    private:
        struct Display {
            Comet::TimingHistory::Summary frame;
            Comet::TimingHistory::Summary cpu;
            Comet::TimingHistory::Summary gpu;
            Comet::MemoryBudgetSnapshot memory;
            bool has_memory = false;
            bool gpu_supported = false;
            bool truncated = false;
            std::string gpu_error;
        };
        void refresh_display(bool capturing);
        const Comet::Engine& m_engine;
        Display m_display;
        double m_next_refresh = 0;
        std::optional<bool> m_capture_request;
        bool m_allocation_report_request = false;
        bool m_paused = false;
        bool m_was_capturing = false;
    };
}
