#pragma once

#include "common/export.h"
#include "graphics/resource/memory_budget.h"
#include "render/render_graph.h"

#include <chrono>
#include <optional>

namespace Comet {
    class Device;
    class FrameScheduler;

    // 主线程拥有的有界场景图诊断；不等待 GPU，不持有 Scene 或 UI。
    class COMET_API RenderDiagnostics {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr uint32_t MAX_PASSES = 32;
        struct PassTiming {
            std::string name;
            double milliseconds = 0;
        };
        struct FrameTiming {
            uint64_t serial = 0;
            double milliseconds = 0;
            std::vector<PassTiming> passes;
            bool truncated = false;
        };
        struct Snapshot {
            std::optional<FrameTiming> cpu;
            std::optional<FrameTiming> gpu;
            MemoryBudgetSnapshot memory;
            uint64_t memory_samples = 0;
            bool gpu_supported = false;
            std::string gpu_error;
        };

        RenderDiagnostics(Device& device, FrameScheduler& frames, bool allow_gpu = true);
        ~RenderDiagnostics() = default;
        RenderDiagnostics(const RenderDiagnostics&) = delete;
        RenderDiagnostics& operator=(const RenderDiagnostics&) = delete;

        void set_enabled(bool enabled);
        [[nodiscard]] bool is_enabled() const { return m_enabled; }
        [[nodiscard]] const Snapshot& get_snapshot() const { return m_snapshot; }
        // 每帧一次；调用者仍负责完成 slot 等待、begin、submit 和 end。
        void record(const RenderGraph::Plan& plan,
            std::span<const RenderGraph::Binding> bindings,
            const RenderGraph::RecordPass& callback);
        void poll_memory(Clock::time_point now = Clock::now());
        // 只读取已被 FrameScheduler 确认完成的 serial，未提交的新查询不会误读旧 availability。
        void collect_completed();
        [[nodiscard]] static std::optional<double> elapsed_ms(
            uint64_t start, uint64_t end, uint32_t valid_bits, double period_nanoseconds);

    private:
        struct Slot;
        void disable_gpu(std::string message);
        Device& m_device;
        FrameScheduler& m_frames;
        std::vector<std::shared_ptr<Slot>> m_slots;
        Snapshot m_snapshot;
        std::optional<Clock::time_point> m_last_memory_sample;
        uint32_t m_valid_bits = 0;
        double m_period_nanoseconds = 0;
        bool m_enabled = false;
        bool m_recording = false;
    };
}
