#include "render/frame_scheduler.h"
#include "core/task_scheduler.h"

#include "diagnostics/logger.h"
#include "graphics/device.h"
#include "graphics/queue.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Comet {
    struct FrameScheduler::Pending {
        struct Commands {
            CommandPool pool;
            CommandBuffer command;
            Commands(Device& device, uint32_t family)
                : pool(device, family), command(pool.allocate_command_buffer()) {}
        };
        std::shared_ptr<Commands> work;
        std::optional<std::future<Result<void, GraphicsError>>> future;
        std::optional<Result<void, GraphicsError>> immediate;
    };
    FrameScheduler::~FrameScheduler() {
        for(auto& pending : m_pending)
            if(pending->future)
                pending->future->wait();
        // 包括录制后未提交的帧；释放池前完成所有队列使用。
        m_device.wait_idle_for_shutdown();
    }
    Result<void, GraphicsError> FrameScheduler::record_independent(RecordIndependent record) {
        using R = Result<void, GraphicsError>;
        if(!is_recording_frame() || !record)
            return R::failure({"Independent recording requires an active frame"});
        auto pending = std::make_unique<Pending>();
        pending->work = std::make_shared<Pending::Commands>(m_device, get_queue_family_index());
        auto task = [work = pending->work, record = std::move(record)]() -> R {
            try {
                work->command.begin(
                    Flags<CommandBuffer::Usage>(CommandBuffer::Usage::OneTimeSubmit));
                auto result = record(work->command);
                if(result)
                    work->command.end();
                return result;
            } catch(const std::exception& error) {
                return R::failure({error.what()});
            }
        };
        if(m_tasks)
            pending->future = m_tasks->try_submit_result(task);
        if(!pending->future)
            pending->immediate = task();
        retain_current_frame_resource(pending->work);
        m_pending.push_back(std::move(pending));
        return R::success();
    }
    Result<std::vector<CommandBuffer>, GraphicsError> FrameScheduler::finish_recordings() {
        using R = Result<std::vector<CommandBuffer>, GraphicsError>;
        std::optional<GraphicsError> failure;
        std::vector<CommandBuffer> commands;
        for(auto& pending : m_pending) {
            const auto result = pending->future ? pending->future->get() : *pending->immediate;
            if(!result)
                failure = result.error();
            commands.push_back(pending->work->command);
        }
        m_pending.clear();
        if(failure)
            return R::failure(*failure);
        return R::success(std::move(commands));
    }
    bool FrameScheduler::is_recording_frame() const {
        return m_frame_active && !m_submission_recorded;
    }

    uint64_t FrameScheduler::get_completed_frame_serial() const {
        return m_completed_frame_serial;
    }

    bool FrameScheduler::is_frame_serial_complete(const uint64_t frame_serial) const {
        return frame_serial == 0 || frame_serial <= m_completed_frame_serial;
    }

    FrameSlot& FrameScheduler::get_current_frame_slot() {
        return m_frame_slots.at(m_current_frame_slot);
    }

    const FrameSlot& FrameScheduler::get_current_frame_slot() const {
        return m_frame_slots.at(m_current_frame_slot);
    }

    SwapchainImageState& FrameScheduler::get_swapchain_image_state(const uint32_t image_index) {
        return m_swapchain_image_states.at(image_index);
    }

    CommandBuffer& FrameScheduler::get_current_command_buffer() {
        return get_current_frame_slot().command_buffer;
    }

    FrameScheduler::FrameScheduler(Device& device, const uint32_t frame_slot_count)
        : m_device(device), m_frame_slot_count(frame_slot_count) {
        if(frame_slot_count == 0) {
            LOG_FATAL("FrameScheduler requires at least one frame slot");
        }

        LOG_INFO("create {} frame slots", frame_slot_count);
        const auto command_buffers =
            device.get_default_command_pool().allocate_command_buffers(frame_slot_count);
        m_frame_slots.reserve(frame_slot_count);
        for(uint32_t index = 0; index < frame_slot_count; ++index) {
            m_frame_slots.emplace_back(device, command_buffers.at(index));
        }
    }

    void FrameScheduler::wait_for_current_slot() {
        if(m_frame_active) {
            LOG_FATAL("Cannot wait for a frame slot while a frame is active");
        }
        if(m_current_slot_ready) {
            return;
        }

        wait_for_slot(m_current_frame_slot);
        m_current_slot_ready = true;
    }

    uint32_t FrameScheduler::get_queue_family_index() const {
        return m_device.get_capability().graphics_queue_family.queue_family_index.value();
    }

    void FrameScheduler::wait_for_all_slots() {
        if(m_frame_active && !m_submission_recorded) {
            LOG_FATAL("Cannot wait for all frame slots before the active frame is submitted");
        }
        for(uint32_t frame_slot = 0; frame_slot < m_frame_slot_count; ++frame_slot) {
            wait_for_slot(frame_slot);
        }
        if(!m_frame_active) {
            m_current_slot_ready = true;
        }
    }

    void FrameScheduler::begin_frame(const uint32_t image_index) {
        if(!m_current_slot_ready || m_frame_active) {
            LOG_FATAL("FrameScheduler requires a ready inactive slot before begin_frame");
        }

        auto& image_state = m_swapchain_image_states.at(image_index);
        if(const auto previous_frame_slot = image_state.in_flight_frame_slot;
            previous_frame_slot && !is_frame_serial_complete(image_state.last_submission_serial)) {
            wait_for_slot(*previous_frame_slot);
        }

        m_current_image_index = image_index;
        m_frame_active = true;
        m_submission_recorded = false;
    }

    void FrameScheduler::retain_current_frame_resource(std::shared_ptr<void> resource) {
        if(!m_frame_active || m_submission_recorded) {
            LOG_FATAL("Frame resources can only be retained while recording an active frame");
        }
        if(!resource) {
            return;
        }

        const void* resource_id = resource.get();
        get_current_frame_slot().retained_resources.try_emplace(resource_id, std::move(resource));
    }

    GpuResourceResult<GpuCompletionPoint> FrameScheduler::submit(
        const std::span<const QueueSemaphoreSubmit> waits,
        const std::span<const QueueSemaphoreSubmit> signals) {
        if(!m_frame_active || m_submission_recorded) {
            LOG_FATAL("FrameScheduler requires one submission for the active frame");
        }

        auto prepared = finish_recordings();
        if(!prepared)
            return GpuResourceResult<GpuCompletionPoint>::failure(
                prepared.error().result.value_or(vk::Result::eErrorUnknown));
        auto commands = std::move(prepared).value();
        std::vector<QueueSemaphoreSubmit> merged(waits.begin(), waits.end());
        if(auto* queue = m_device.get_async_compute_queue(); queue && !commands.empty()) {
            const auto completion = queue->submit2({}, commands, {}, nullptr);
            if(!completion) {
                m_device.wait_idle_for_shutdown();
                return GpuResourceResult<GpuCompletionPoint>::failure(completion.result());
            }
            merge_semaphore_wait(merged, QueueSemaphoreSubmit(completion.value(),
                                             Flags<PipelineStage>(PipelineStage::AllCommands)));
            commands.clear();
        }
        auto& slot = get_current_frame_slot();
        commands.push_back(slot.command_buffer);
        m_device.reset_fences(std::span(&slot.in_flight_fence, 1));
        const auto completion = m_device.get_graphics_queue(0).submit2(
            merged, commands, signals, &slot.in_flight_fence);
        if(!completion) {
            m_device.wait_idle_for_shutdown();
            slot.retained_resources.clear();
            m_frame_active = false;
            m_current_slot_ready = false;
            return completion;
        }

        m_submission_recorded = true;
        slot.last_submission_serial = m_current_frame_serial;
        m_swapchain_image_states[m_current_image_index].in_flight_frame_slot = m_current_frame_slot;
        m_swapchain_image_states[m_current_image_index].last_submission_serial =
            m_current_frame_serial;
        return completion;
    }

    void FrameScheduler::end_frame() {
        if(!m_frame_active || !m_submission_recorded) {
            LOG_FATAL("Cannot end a frame before recording its submission");
        }
        if(m_current_frame_serial == std::numeric_limits<uint64_t>::max()) {
            LOG_FATAL("Frame submission serial exhausted");
        }

        ++m_current_frame_serial;
        m_current_frame_slot = (m_current_frame_slot + 1) % m_frame_slot_count;
        m_current_slot_ready = false;
        m_frame_active = false;
        m_submission_recorded = false;
    }

    void FrameScheduler::initialize_swapchain_images(const uint32_t image_count) {
        if(image_count == 0) {
            LOG_FATAL("FrameScheduler requires at least one swapchain image");
        }

        LOG_INFO(
            "create {} swapchain image states for {} frame slots", image_count, m_frame_slot_count);
        m_swapchain_image_states.clear();
        m_swapchain_image_states.reserve(image_count);
        for(uint32_t index = 0; index < image_count; ++index) {
            m_swapchain_image_states.emplace_back(m_device);
        }
    }

    void FrameScheduler::wait_for_slot(const uint32_t frame_slot_index) {
        auto& slot = m_frame_slots.at(frame_slot_index);
        // 重置后未成功提交的 fence 不会收到完成信号，不能等待。
        if(!is_frame_serial_complete(slot.last_submission_serial))
            m_device.wait_for_fences(std::span(&slot.in_flight_fence, 1));
        // Release owners at completion; the slot's pool reuses node allocations.
        slot.retained_resources.clear();
        m_completed_frame_serial = std::max(m_completed_frame_serial, slot.last_submission_serial);
    }
}
