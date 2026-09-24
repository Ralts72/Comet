#pragma once

#include "common/export.h"
#include "diagnostics/timing_history.h"

#include <optional>

namespace Comet {
    // 只记录主循环的 CPU 阶段；渲染图与 GPU 时间由 RenderDiagnostics 负责。
    class COMET_API FrameDiagnostics final {
    public:
        struct Timing {
            int frame_index = 0;
            double events_ms = 0;
            double update_ms = 0;
            double prepare_ms = 0;
            double render_submit_ms = 0;
            double total_ms = 0;
            bool rendered = false;
        };

        void begin_frame(bool enabled);
        void set_frame_index(int index);
        void mark_events();
        void mark_update();
        void mark_prepare();
        void mark_deferred_wait();
        void mark_runtime_update();
        void mark_render_submit();
        void finish_frame(bool rendered, bool still_enabled);
        void cancel_pending_frame();
        void clear_current();

        [[nodiscard]] const std::optional<Timing>& current() const { return m_current; }
        [[nodiscard]] const TimingHistory& history() const { return m_history; }

    private:
        [[nodiscard]] double elapsed_phase_ms();

        bool m_recording = false;
        TimingHistory::Clock::time_point m_phase_start{};
        Timing m_pending;
        std::optional<Timing> m_current;
        TimingHistory m_history;
    };
}
