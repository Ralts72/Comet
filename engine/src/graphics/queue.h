#pragma once
#include "graphics/synchronization/gpu_completion_point.h"
#include "graphics/synchronization/semaphore.h"
#include "vk_common.h"

#include <memory>

namespace Comet {
    class Device;
    class CommandBuffer;
    class Fence;
    class Swapchain;
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
