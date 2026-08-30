#pragma once
#include "graphics/synchronization/semaphore.h"
#include "vk_common.h"

#include <limits>
#include <memory>

namespace Comet {
    class Device;
    class CommandBuffer;
    class Fence;
    class Swapchain;
    class Queue;

    class GpuCompletionPoint {
    public:
        GpuCompletionPoint() = default;

        [[nodiscard]] bool is_valid() const noexcept {
            return m_timeline != nullptr && m_value > 0;
        }

        [[nodiscard]] uint64_t get_value() const noexcept { return m_value; }
        [[nodiscard]] bool is_complete() const;
        [[nodiscard]] bool wait(
            uint64_t timeout = std::numeric_limits<uint64_t>::max()) const;

        bool operator==(const GpuCompletionPoint&) const noexcept = default;

    private:
        friend class Queue;
        friend struct QueueSemaphoreSubmit;

        GpuCompletionPoint(
            const Semaphore& timeline,
            const uint64_t value)
            : m_timeline(&timeline), m_value(value) {}

        const Semaphore* m_timeline = nullptr;
        uint64_t m_value = 0;
    };

    struct QueueSemaphoreSubmit {
        const Semaphore* semaphore;
        uint64_t value;
        Flags<PipelineStage> stage_mask;

        QueueSemaphoreSubmit(const Semaphore& semaphore,
                             const Flags<PipelineStage> stage_mask,
                             const uint64_t value = 0)
            : semaphore(&semaphore), value(value), stage_mask(stage_mask) {}

        QueueSemaphoreSubmit(
            const GpuCompletionPoint& completion,
            Flags<PipelineStage> stage_mask);
    };

    class Queue {
    public:
        Queue(Device& device, vk::Queue queue);

        ~Queue() = default;

        Queue(const Queue&) = delete;
        Queue& operator=(const Queue&) = delete;
        Queue(Queue&&) noexcept = default;
        Queue& operator=(Queue&&) noexcept = delete;

        [[nodiscard]] GpuCompletionPoint submit2(
            std::span<const QueueSemaphoreSubmit> waits,
            std::span<const CommandBuffer> command_buffers,
            std::span<const QueueSemaphoreSubmit> signals,
            const Fence* fence);

        [[nodiscard]] vk::Result present(const Swapchain& swapchain, std::span<const Semaphore> wait_semaphores,
                                         uint32_t image_index) const;

        [[nodiscard]] vk::Queue get() const { return m_queue; }

    private:
        vk::Queue m_queue;
        std::unique_ptr<Semaphore> m_completion_timeline;
        uint64_t m_next_completion_value = 1;
    };
}
