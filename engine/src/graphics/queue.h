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
        enum class Type { Graphics, Present, Transfer, Compute };

        Queue(uint32_t family_index, uint32_t index, vk::Queue queue, Type type);

        ~Queue() = default;

        void wait_idle() const { m_queue.waitIdle(); }

        void submit2(std::span<const QueueSemaphoreSubmit> waits,
                     std::span<const CommandBuffer> command_buffers,
                     std::span<const QueueSemaphoreSubmit> signals,
                     const Fence* fence) const;

        [[nodiscard]] vk::Result present(const Swapchain& swapchain, std::span<const Semaphore> wait_semaphores,
                                         uint32_t image_index) const;

        [[nodiscard]] vk::Queue get() const { return m_queue; }
        [[nodiscard]] uint32_t get_family_index() const { return m_family_index; }
        [[nodiscard]] uint32_t get_index() const { return m_index; }
        [[nodiscard]] Type get_type() const { return m_type; }

    private:
        uint32_t m_family_index;
        uint32_t m_index;
        vk::Queue m_queue;
        Type m_type;
    };
}
