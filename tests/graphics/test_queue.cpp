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
        concept SupportsSynchronization2Submit = requires(
            const T& queue,
            std::span<const QueueSemaphoreSubmit> waits,
            std::span<const CommandBuffer> command_buffers,
            std::span<const QueueSemaphoreSubmit> signals,
            const Fence* fence) {
            queue.submit2(waits, command_buffers, signals, fence);
        };

        template<typename T>
        concept SupportsLegacyNoWaitSubmit = requires(
            const T& queue,
            std::span<const CommandBuffer> command_buffers,
            std::span<const Semaphore> signal_semaphores,
            const Fence* fence) {
            queue.submit(command_buffers, signal_semaphores, fence);
        };

        template<typename T>
        concept SupportsLegacySingleWaitSubmit = requires(
            const T& queue,
            std::span<const CommandBuffer> command_buffers,
            const Semaphore& wait_semaphore,
            std::span<const Semaphore> signal_semaphores,
            const Fence* fence) {
            queue.submit(command_buffers, wait_semaphore, signal_semaphores, fence);
        };
    }

    TEST(QueueSubmitInterfaceTest, UsesSynchronization2SubmissionModel) {
        EXPECT_TRUE(SupportsSynchronization2Submit<Queue>);
        EXPECT_FALSE(SupportsLegacyNoWaitSubmit<Queue>);
        EXPECT_FALSE(SupportsLegacySingleWaitSubmit<Queue>);
    }
}
