#include "render/render_diagnostics.h"

#include "graphics/device.h"
#include "render/frame_scheduler.h"
#include "diagnostics/logger.h"

#include <array>
#include <cmath>
#include <stdexcept>

namespace Comet {
    struct RenderDiagnostics::Slot {
        vk::UniqueQueryPool queries;
        std::optional<FrameTiming> pending;
    };

    RenderDiagnostics::RenderDiagnostics(
        Device& device, FrameScheduler& frames, const bool allow_gpu)
        : m_device(device), m_frames(frames), m_slots(frames.get_frame_slot_count()) {
        const auto& capability = device.get_capability();
        const auto queue_family =
            capability.graphics_queue_family.queue_family_index.value();
        if(frames.get_queue_family_index() != queue_family)
            throw std::invalid_argument(
                "Render diagnostics requires the graphics queue family");
        const auto physical = capability.physical_device;
        m_valid_bits =
            physical.getQueueFamilyProperties().at(queue_family).timestampValidBits;
        m_period_nanoseconds = physical.getProperties().limits.timestampPeriod;
        m_snapshot.gpu_supported =
            allow_gpu && elapsed_ms(0, 0, m_valid_bits, m_period_nanoseconds).has_value();
    }

    void RenderDiagnostics::set_enabled(const bool enabled) {
        if(m_recording)
            throw std::logic_error("Cannot change diagnostics during graph recording");
        m_enabled = enabled;
    }

    std::optional<double> RenderDiagnostics::elapsed_ms(const uint64_t start,
        const uint64_t end, const uint32_t valid_bits, const double period_nanoseconds) {
        if(valid_bits == 0 || valid_bits > 64 || !std::isfinite(period_nanoseconds)
            || period_nanoseconds <= 0)
            return std::nullopt;
        const uint64_t mask =
            valid_bits == 64 ? ~uint64_t(0) : (uint64_t(1) << valid_bits) - 1;
        const double result =
            double((end - start) & mask) * period_nanoseconds / 1000000.0;
        if(!std::isfinite(result))
            return std::nullopt;
        return result;
    }

    void RenderDiagnostics::disable_gpu(std::string message) {
        if(m_snapshot.gpu_error.empty())
            LOG_WARN("GPU timing disabled: {}", message);
        m_snapshot.gpu_error = std::move(message);
    }

    void RenderDiagnostics::poll_memory(const Clock::time_point now) {
        if(!m_enabled
            || (m_last_memory_sample
                && now - *m_last_memory_sample < std::chrono::seconds(1)))
            return;
        m_snapshot.memory = m_device.query_memory_budget();
        ++m_snapshot.memory_samples;
        m_last_memory_sample = now;
    }

    void RenderDiagnostics::collect_completed() {
        if(m_recording)
            throw std::logic_error("Cannot collect queries during graph recording");
        for(const auto& slot : m_slots) {
            if(!slot || !slot->pending
                || !m_frames.is_frame_serial_complete(slot->pending->serial))
                continue;
            const auto count = static_cast<uint32_t>(slot->pending->passes.size() + 1);
            struct QueryResult {
                uint64_t value = 0;
                uint64_t available = 0;
            };
            std::array<QueryResult, MAX_PASSES + 1> results{};
            const auto result = m_device.get().getQueryPoolResults(*slot->queries, 0,
                count, sizeof(QueryResult) * count, results.data(), sizeof(QueryResult),
                vk::QueryResultFlagBits::e64
                    | vk::QueryResultFlagBits::eWithAvailability);
            if(result == vk::Result::eNotReady)
                continue;
            if(result == vk::Result::eErrorDeviceLost)
                throw vk::DeviceLostError("GPU timing query");
            if(result != vk::Result::eSuccess) {
                disable_gpu(vk::to_string(result));
                slot->pending.reset();
                continue;
            }
            bool available = true;
            for(uint32_t index = 0; index < count; ++index)
                available &= results[index].available != 0;
            if(!available)
                continue;
            auto timing = std::move(*slot->pending);
            slot->pending.reset();
            timing.milliseconds = *elapsed_ms(results[0].value, results[count - 1].value,
                m_valid_bits, m_period_nanoseconds);
            for(size_t index = 0; index < timing.passes.size(); ++index)
                timing.passes[index].milliseconds = *elapsed_ms(results[index].value,
                    results[index + 1].value, m_valid_bits, m_period_nanoseconds);
            if(!m_snapshot.gpu || timing.serial > m_snapshot.gpu->serial)
                m_snapshot.gpu = std::move(timing);
        }
    }

    void RenderDiagnostics::record(const RenderGraph::Plan& plan,
        const std::span<const RenderGraph::Binding> bindings,
        const RenderGraph::RecordPass& callback) {
        if(m_recording)
            throw std::logic_error(
                "Render diagnostics does not support nested recording");
        if(!m_enabled) {
            m_recording = true;
            try {
                plan.record(m_frames, bindings, callback);
            } catch(...) {
                m_recording = false;
                throw;
            }
            m_recording = false;
            return;
        }
        auto& frames = m_frames;
        if(!frames.is_recording_frame())
            throw std::invalid_argument("Invalid frame scheduler for render diagnostics");
        collect_completed();
        poll_memory();
        const auto passes = plan.get_passes();
        FrameTiming timing{.serial = frames.get_current_frame_serial(),
            .truncated = passes.size() > MAX_PASSES};
        for(size_t index = 0; index < std::min<size_t>(passes.size(), MAX_PASSES);
            ++index)
            timing.passes.push_back({passes[index].name, 0});
        std::shared_ptr<Slot> gpu;
        if(m_snapshot.gpu_supported && m_snapshot.gpu_error.empty() && !timing.truncated
            && !passes.empty()) {
            auto& slot = m_slots.at(frames.get_current_frame_slot_index());
            if(slot && slot->pending
                && !frames.is_frame_serial_complete(slot->pending->serial))
                throw std::logic_error("Only one measured graph per frame is supported");
            if(!slot) {
                try {
                    auto candidate = std::make_shared<Slot>();
                    candidate->queries = m_device.get().createQueryPoolUnique(
                        {{}, vk::QueryType::eTimestamp, MAX_PASSES + 1});
                    slot = std::move(candidate);
                } catch(const vk::SystemError& error) {
                    if(error.code().value()
                        == static_cast<int>(vk::Result::eErrorDeviceLost))
                        throw;
                    disable_gpu(error.what());
                }
            }
            gpu = slot;
        }
        auto& command = frames.get_current_command_buffer();
        if(gpu) {
            gpu->pending.reset();
            frames.retain_current_frame_resource(gpu);
            command.get().resetQueryPool(
                *gpu->queries, 0, static_cast<uint32_t>(passes.size() + 1));
            command.get().writeTimestamp2(
                vk::PipelineStageFlagBits2::eTopOfPipe, *gpu->queries, 0);
        }
        m_recording = true;
        const auto start = Clock::now();
        try {
            plan.record(
                frames, bindings, [&](size_t index, const CommandBuffer& commands) {
                    const auto pass_start = Clock::now();
                    callback(index, commands);
                    if(index < timing.passes.size())
                        timing.passes[index].milliseconds =
                            std::chrono::duration<double, std::milli>(
                                Clock::now() - pass_start)
                                .count();
                    if(gpu)
                        commands.get().writeTimestamp2(
                            vk::PipelineStageFlagBits2::eAllCommands, *gpu->queries,
                            static_cast<uint32_t>(index + 1));
                });
        } catch(...) {
            m_recording = false;
            throw;
        }
        timing.milliseconds =
            std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        m_recording = false;
        if(gpu)
            gpu->pending = timing;
        m_snapshot.cpu = std::move(timing);
    }
}
