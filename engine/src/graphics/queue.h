#pragma once
#include "vk_common.h"

namespace Comet {
    class CommandBuffer;
    class Semaphore;
    class Fence;
    class Swapchain;

    class Queue {
    public:
        enum class Type { Graphics, Present, transfer, Compute };
        Queue(uint32_t family_index, uint32_t index, vk::Queue queue, Type type);

        ~Queue() = default;

        void wait_idle() const { m_queue.waitIdle(); }
        void submit(std::span<const CommandBuffer> command_buffers, std::span<const Semaphore> wait_semaphores,
            std::span<const Semaphore> signal_semaphores, const Fence* fence) const;

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
