#include <gtest/gtest.h>

#include "graphics/command_buffer.h"
#include "graphics/fence.h"
#include "graphics/queue.h"
#include "graphics/semaphore.h"

#include <concepts>
#include <span>

namespace Comet::Tests {
    namespace {
        template<typename T>
        concept SupportsNoWaitSubmit = requires(
            const T& queue,
            std::span<const CommandBuffer> command_buffers,
            std::span<const Semaphore> signal_semaphores,
            const Fence* fence) {
            queue.submit(command_buffers, signal_semaphores, fence);
        };

        template<typename T>
        concept SupportsSingleWaitSubmit = requires(
            const T& queue,
            std::span<const CommandBuffer> command_buffers,
            const Semaphore& wait_semaphore,
            std::span<const Semaphore> signal_semaphores,
            const Fence* fence) {
            queue.submit(command_buffers, wait_semaphore, signal_semaphores, fence);
        };

        template<typename T>
        concept SupportsWaitSemaphoreSpan = requires(
            const T& queue,
            std::span<const CommandBuffer> command_buffers,
            std::span<const Semaphore> wait_semaphores,
            std::span<const Semaphore> signal_semaphores,
            const Fence* fence) {
            queue.submit(command_buffers, wait_semaphores, signal_semaphores, fence);
        };
    }

    TEST(QueueSubmitInterfaceTest, SupportsOnlyZeroOrOneWaitSemaphore) {
        EXPECT_TRUE(SupportsNoWaitSubmit<Queue>);
        EXPECT_TRUE(SupportsSingleWaitSubmit<Queue>);
        EXPECT_FALSE(SupportsWaitSemaphoreSpan<Queue>);
    }
}
