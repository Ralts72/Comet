#include "diagnostics/frame_diagnostics.h"

namespace Comet {
    void FrameDiagnostics::begin_frame(bool enabled) {
        m_recording = enabled;
        m_pending = {};
        if(enabled)
            m_phase_start = TimingHistory::Clock::now();
        else
            clear_current();
    }

    void FrameDiagnostics::set_frame_index(int index) {
        if(m_recording)
            m_pending.frame_index = index;
    }

    double FrameDiagnostics::elapsed_phase_ms() {
        if(!m_recording)
            return 0;
        const auto now = TimingHistory::Clock::now();
        const auto duration = std::chrono::duration<double, std::milli>(now - m_phase_start);
        m_phase_start = now;
        return duration.count();
    }

    void FrameDiagnostics::mark_events() {
        m_pending.events_ms = elapsed_phase_ms();
    }

    void FrameDiagnostics::mark_update() {
        m_pending.update_ms = elapsed_phase_ms();
    }

    void FrameDiagnostics::mark_prepare() {
        m_pending.prepare_ms = elapsed_phase_ms();
    }

    void FrameDiagnostics::mark_deferred_wait() {
        m_pending.prepare_ms += elapsed_phase_ms();
    }

    void FrameDiagnostics::mark_runtime_update() {
        m_pending.update_ms += elapsed_phase_ms();
    }

    void FrameDiagnostics::mark_render_submit() {
        m_pending.render_submit_ms = elapsed_phase_ms();
    }

    void FrameDiagnostics::finish_frame(bool rendered, bool still_enabled) {
        if(!m_recording || !still_enabled) {
            clear_current();
            return;
        }
        m_pending.rendered = rendered;
        m_pending.total_ms = m_pending.events_ms + m_pending.update_ms + m_pending.prepare_ms
                             + m_pending.render_submit_ms;
        if(!m_current)
            m_history.clear();
        const TimingHistory::Entry phases[]{{"Events", m_pending.events_ms},
            {"Update", m_pending.update_ms}, {"Prepare / UI", m_pending.prepare_ms},
            {"Render / submit", m_pending.render_submit_ms}};
        m_history.record(m_pending.total_ms, phases);
        m_current = m_pending;
        m_recording = false;
    }

    void FrameDiagnostics::clear_current() {
        m_current.reset();
        m_recording = false;
    }
}
