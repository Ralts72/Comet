#include "render/render_diagnostics.h"
#include "common/scope_exit.h"
#include "graphics/device.h"
#include "graphics/gpu_timer.h"
#include "render/frame_scheduler.h"
#include "diagnostics/logger.h"

#include <algorithm>
#include <array>

namespace Comet {
    struct RenderDiagnostics::Slot {
        std::shared_ptr<GpuTimer> queries;
        std::optional<GraphTiming> pending;
    };

    RenderDiagnostics::RenderDiagnostics(FrameScheduler& frames, bool allow_gpu)
        : m_frames(frames), m_slots(frames.get_frame_slot_count()) {
        const auto capabilities =
            GpuTimer::capabilities(frames.get_device(), frames.get_queue_family_index());
        m_valid_bits = capabilities.valid_bits;
        m_period_nanoseconds = capabilities.period_nanoseconds;
        m_snapshot.gpu_supported =
            allow_gpu && GpuTimer::elapsed_ms(0, 0, capabilities).has_value();
    }

    Result<void> RenderDiagnostics::set_enabled(bool enabled) {
        if(m_recording || m_frames.is_frame_active())
            return Result<void>::failure("Diagnostics changes require a frame boundary");
        if(enabled && !m_enabled) {
            m_cpu_history.clear();
            m_gpu_history.clear();
            m_snapshot.cpu.reset();
            m_snapshot.gpu.reset();
            for(const auto& slot : m_slots)
                if(slot)
                    slot->pending.reset();
        }
        m_enabled = enabled;
        return Result<void>::success();
    }

    Result<std::string> RenderDiagnostics::build_allocation_report() const {
        return m_frames.get_device().build_allocation_report();
    }

    void RenderDiagnostics::disable_gpu(std::string message) {
        if(m_snapshot.gpu_error.empty())
            LOG_WARN("GPU timing disabled: {}", message);
        m_snapshot.gpu_error = std::move(message);
    }

    void RenderDiagnostics::poll_memory(Clock::time_point now) {
        if(!m_enabled
            || (m_last_memory_sample && now - *m_last_memory_sample < std::chrono::seconds(1)))
            return;
        m_snapshot.memory = m_frames.get_device().query_memory_budget();
        ++m_snapshot.memory_samples;
        m_last_memory_sample = now;
    }

    Result<void, GraphicsError> RenderDiagnostics::collect_completed() {
        if(m_recording)
            return Result<void, GraphicsError>::failure({"Cannot collect during graph recording"});
        if(!m_enabled || !m_snapshot.gpu_error.empty())
            return Result<void, GraphicsError>::success();
        for(const auto& slot : m_slots) {
            if(!slot || !slot->pending || !m_frames.is_frame_serial_complete(slot->pending->serial))
                continue;
            std::array<GpuTimer::Sample, MAX_PASSES + 2> samples{};
            const auto count = slot->pending->passes.size() + 2;
            const auto read = slot->queries->read(std::span(samples).first(count));
            if(!read) {
                if(read.error().is_device_lost())
                    return Result<void, GraphicsError>::failure(read.error());
                disable_gpu(read.error().message);
                slot->pending.reset();
                break;
            }
            if(!read.value())
                continue;
            auto timing = std::move(*slot->pending);
            slot->pending.reset();
            const auto elapsed = [&](size_t first, size_t last) {
                return *GpuTimer::elapsed_ms(samples[first].ticks, samples[last].ticks,
                    {m_valid_bits, m_period_nanoseconds});
            };
            timing.milliseconds = elapsed(0, count - 1);
            for(size_t index = 0; index < timing.passes.size(); ++index)
                timing.passes[index].milliseconds = elapsed(index, index + 1);
            m_gpu_history.record(timing.milliseconds, timing.passes);
            if(!m_snapshot.gpu || timing.serial > m_snapshot.gpu->serial)
                m_snapshot.gpu = std::move(timing);
        }
        return Result<void, GraphicsError>::success();
    }

    void RenderDiagnostics::skip_frame() {
        m_snapshot.scene_rendered = false;
        m_snapshot.cpu.reset();
        m_snapshot.gpu.reset();
        // 保留历史窗口，但不把隐藏前的在途采样发布为当前场景数据。
        for(const auto& slot : m_slots)
            if(slot)
                slot->pending.reset();
    }

    Result<void, GraphicsError> RenderDiagnostics::record(const RenderGraph::Plan& plan,
        std::span<const RenderGraph::Binding> bindings, const RenderGraph::RecordPass& callback) {
        if(m_recording || !m_frames.is_recording_frame() || !callback)
            return Result<void, GraphicsError>::failure({"Invalid diagnostics recording context"});
        if(!m_enabled) {
            m_recording = true;
            const ScopeExit finish([&] { m_recording = false; });
            auto recorded = plan.record(m_frames, bindings, callback);
            m_snapshot.scene_rendered = static_cast<bool>(recorded);
            return recorded;
        }
        const auto serial = m_frames.get_current_frame_serial();
        if(m_last_recorded_serial == serial)
            return Result<void, GraphicsError>::failure(
                {"Only one measured graph per frame is supported"});
        if(auto completed = collect_completed(); !completed)
            return completed;
        poll_memory();
        const auto passes = plan.get_passes();
        GraphTiming timing{.serial = serial, .truncated = passes.size() > MAX_PASSES};
        for(size_t index = 0; index < std::min<size_t>(passes.size(), MAX_PASSES); ++index)
            timing.passes.push_back({passes[index].name.substr(0, MAX_LABEL_LENGTH), 0});
        std::shared_ptr<Slot> gpu;
        if(m_snapshot.gpu_supported && m_snapshot.gpu_error.empty() && !timing.truncated
            && !passes.empty()) {
            auto& slot = m_slots[m_frames.get_current_frame_slot_index()];
            if(!slot) {
                auto queries = GpuTimer::create(m_frames.get_device(), MAX_PASSES + 2);
                if(!queries) {
                    if(queries.error().is_device_lost())
                        return Result<void, GraphicsError>::failure(queries.error());
                    disable_gpu(queries.error().message);
                } else {
                    slot = std::make_shared<Slot>();
                    slot->queries = std::move(queries).value();
                }
            }
            gpu = slot;
        }
        auto& command = m_frames.get_current_command_buffer();
        if(gpu) {
            // 槽位已由调度器等待完成；未读到的旧样本可丢弃，但不能凭 availability 判断新帧完成。
            gpu->pending.reset();
            m_frames.retain_current_frame_resource(gpu->queries);
            gpu->queries->reset(command);
            gpu->queries->write(command, 0, GpuTimer::Boundary::Begin);
        }
        m_recording = true;
        m_last_recorded_serial = serial;
        const ScopeExit finish([&] { m_recording = false; });
        const auto start = Clock::now();
        auto recorded = plan.record(m_frames, bindings, [&](size_t index, CommandBuffer& commands) {
            const auto pass_start = Clock::now();
            auto result = callback(index, commands);
            if(!result)
                return result;
            if(index < timing.passes.size())
                timing.passes[index].milliseconds =
                    std::chrono::duration<double, std::milli>(Clock::now() - pass_start).count();
            if(gpu)
                gpu->queries->write(
                    commands, static_cast<uint32_t>(index + 1), GpuTimer::Boundary::End);
            return Result<void, GraphicsError>::success();
        });
        if(!recorded)
            return recorded;
        if(gpu) {
            // 图末尾的导出屏障也属于总耗时，但不计入最后一个 Pass 的回调耗时。
            gpu->queries->write(
                command, static_cast<uint32_t>(passes.size() + 1), GpuTimer::Boundary::End);
            gpu->pending = timing;
        }
        timing.milliseconds =
            std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        m_cpu_history.record(timing.milliseconds, timing.passes);
        m_snapshot.cpu = std::move(timing);
        m_snapshot.scene_rendered = true;
        return Result<void, GraphicsError>::success();
    }
}
