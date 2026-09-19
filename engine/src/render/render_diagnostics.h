#pragma once

#include "common/export.h"
#include "diagnostics/timing_history.h"
#include "graphics/resource/memory_budget.h"
#include "render/render_graph.h"

#include <chrono>
#include <optional>

namespace Comet {
    // 只观察既有帧生命周期，不额外等待 GPU；由渲染所属线程访问。
    class COMET_API RenderDiagnostics {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr uint32_t MAX_PASSES = 32;
        static constexpr uint32_t MAX_LABEL_LENGTH = 128;
        using PassTiming = TimingHistory::Entry;
        struct GraphTiming {
            uint64_t serial = 0;
            double milliseconds = 0;
            std::vector<PassTiming> passes;
            bool truncated = false;
        };
        struct Snapshot {
            std::optional<GraphTiming> cpu;
            std::optional<GraphTiming> gpu;
            MemoryBudgetSnapshot memory;
            uint64_t memory_samples = 0;
            bool gpu_supported = false;
            std::string gpu_error;
        };

        explicit RenderDiagnostics(FrameScheduler& frames, bool allow_gpu = true);
        ~RenderDiagnostics() = default;
        RenderDiagnostics(const RenderDiagnostics&) = delete;
        RenderDiagnostics& operator=(const RenderDiagnostics&) = delete;
        [[nodiscard]] Result<void> set_enabled(bool enabled);
        [[nodiscard]] bool is_enabled() const { return m_enabled; }
        [[nodiscard]] const Snapshot& get_snapshot() const { return m_snapshot; }
        [[nodiscard]] const TimingHistory& cpu_history() const { return m_cpu_history; }
        [[nodiscard]] const TimingHistory& gpu_history() const { return m_gpu_history; }
        [[nodiscard]] Result<void, GraphicsError> record(const RenderGraph::Plan& plan,
            std::span<const RenderGraph::Binding> bindings,
            const RenderGraph::RecordPass& callback);
        void poll_memory(Clock::time_point now = Clock::now());
        [[nodiscard]] Result<void, GraphicsError> collect_completed();

    private:
        struct Slot;
        void disable_gpu(std::string message);
        FrameScheduler& m_frames;
        std::vector<std::shared_ptr<Slot>> m_slots;
        Snapshot m_snapshot;
        TimingHistory m_cpu_history;
        TimingHistory m_gpu_history;
        std::optional<Clock::time_point> m_last_memory_sample;
        uint64_t m_last_recorded_serial = 0;
        uint32_t m_valid_bits = 0;
        double m_period_nanoseconds = 0;
        bool m_enabled = false;
        bool m_recording = false;
    };
}
