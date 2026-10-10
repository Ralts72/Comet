#pragma once

#include "common/export.h"
#include "common/scope_exit.h"
#include "diagnostics/timing_history.h"
#include "graphics/resource/memory_budget.h"
#include "render/render_graph.h"

#include <chrono>
#include <optional>
#include <string>
#include <utility>

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
        enum class PreparationPhase { Assets, MaterialPrograms, Geometry, Lighting };
        struct PreparationTiming {
            uint64_t serial = 0;
            double assets_ms = 0;
            double material_programs_ms = 0;
            double geometry_ms = 0;
            double lighting_ms = 0;
        };
        struct SubmissionTiming {
            uint64_t serial = 0;
            double finalize_ms = 0;
            double submit_ms = 0;
            double present_ms = 0;
        };
        struct Snapshot {
            bool scene_rendered = false;
            std::optional<GraphTiming> cpu;
            std::optional<GraphTiming> gpu;
            std::optional<PreparationTiming> preparation;
            std::optional<SubmissionTiming> submission;
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
        [[nodiscard]] Result<std::string> build_allocation_report() const;
        [[nodiscard]] bool is_enabled() const { return m_enabled; }
        [[nodiscard]] const Snapshot& get_snapshot() const { return m_snapshot; }
        [[nodiscard]] const TimingHistory& cpu_history() const { return m_cpu_history; }
        [[nodiscard]] const TimingHistory& gpu_history() const { return m_gpu_history; }
        [[nodiscard]] const TimingHistory& preparation_history() const {
            return m_preparation_history;
        }
        [[nodiscard]] const TimingHistory& submission_history() const {
            return m_submission_history;
        }
        [[nodiscard]] Result<void, GraphicsError> record(const RenderGraph::Plan& plan,
            std::span<const RenderGraph::Binding> bindings,
            const RenderGraph::RecordPass& callback);
        void poll_memory(Clock::time_point now = Clock::now());
        [[nodiscard]] Result<void, GraphicsError> collect_completed();
        void skip_frame();
        // 仅在成功提交后与同帧场景图配对；分开命令结束、提交和呈现。
        void record_submission(double finalize_ms, double submit_ms, double present_ms);

        // 同步执行既有准备步骤；只随成功的场景图发布，关闭诊断时不读取时钟。
        template<typename Function>
        static decltype(auto) measure_preparation(
            RenderDiagnostics* diagnostics, PreparationPhase phase, Function&& function) {
            const auto start = diagnostics ? diagnostics->begin_preparation_phase() : std::nullopt;
            const ScopeExit finish([&] {
                if(start)
                    diagnostics->end_preparation_phase(phase, *start);
            });
            return std::forward<Function>(function)();
        }

    private:
        struct Slot;
        void disable_gpu(std::string message);
        [[nodiscard]] std::optional<Clock::time_point> begin_preparation_phase();
        void end_preparation_phase(PreparationPhase phase, Clock::time_point start);
        FrameScheduler& m_frames;
        std::vector<std::shared_ptr<Slot>> m_slots;
        Snapshot m_snapshot;
        PreparationTiming m_pending_preparation;
        TimingHistory m_cpu_history;
        TimingHistory m_gpu_history;
        TimingHistory m_preparation_history;
        TimingHistory m_submission_history;
        std::optional<Clock::time_point> m_last_memory_sample;
        uint64_t m_last_recorded_serial = 0;
        uint32_t m_valid_bits = 0;
        double m_period_nanoseconds = 0;
        bool m_enabled = false;
        bool m_recording = false;
    };
}
