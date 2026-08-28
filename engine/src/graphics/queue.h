#pragma once
#include "vk_common.h"

namespace Comet {
    class CommandBuffer;
    class Semaphore;
    class Fence;
    class Swapchain;

    struct QueueSemaphoreSubmit {
        const Semaphore& semaphore;
        uint64_t value;
        Flags<PipelineStage> stage_mask;

        QueueSemaphoreSubmit(const Semaphore& semaphore,
                             const Flags<PipelineStage> stage_mask,
                             const uint64_t value = 0)
            : semaphore(semaphore), value(value), stage_mask(stage_mask) {}
    };

    class Queue {
    public:
        explicit Queue(vk::Queue queue): m_queue(queue) {}

        ~Queue() = default;

        void wait_idle() const { m_queue.waitIdle(); }

        void submit2(std::span<const QueueSemaphoreSubmit> waits,
                     std::span<const CommandBuffer> command_buffers,
                     std::span<const QueueSemaphoreSubmit> signals,
                     const Fence* fence) const;

        [[nodiscard]] vk::Result present(const Swapchain& swapchain, std::span<const Semaphore> wait_semaphores,
                                         uint32_t image_index) const;

        [[nodiscard]] vk::Queue get() const { return m_queue; }

    private:
        vk::Queue m_queue;
    };
}
