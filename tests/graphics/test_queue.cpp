#include <gtest/gtest.h>

#include "graphics/command/command_buffer.h"
#include "graphics/synchronization/fence.h"
#include "graphics/queue.h"
#include "graphics/synchronization/semaphore.h"

#include <concepts>
#include <span>

namespace Comet::Tests {
    namespace {
        template<typename T>
        concept SupportsExplicitWaitModes = requires(
            const T& completion,
            uint64_t timeout) {
            { completion.wait() } -> std::same_as<void>;
            { completion.wait_for(timeout) } -> std::same_as<bool>;
        };

        template<typename T>
        concept SupportsTimelineWaitModes = requires(
            const T& semaphore,
            uint64_t value,
            uint64_t timeout) {
            { semaphore.wait(value) } -> std::same_as<void>;
            { semaphore.wait_for(value, timeout) } -> std::same_as<bool>;
        };

        template<typename T>
        concept SupportsSynchronization2Submit = requires(
            T& queue,
            std::span<const QueueSemaphoreSubmit> waits,
            std::span<const CommandBuffer> command_buffers,
            std::span<const QueueSemaphoreSubmit> signals,
            const Fence* fence) {
            {
                queue.submit2(waits, command_buffers, signals, fence)
            } -> std::same_as<GpuCompletionPoint>;
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
        EXPECT_FALSE(GpuCompletionPoint{}.is_valid());
        EXPECT_TRUE((std::is_constructible_v<
            QueueSemaphoreSubmit,
            const GpuCompletionPoint&,
            Flags<PipelineStage>>));
        EXPECT_TRUE((std::is_constructible_v<
            Semaphore,
            Device&,
            Semaphore::Type,
            uint64_t>));
        EXPECT_TRUE(SupportsExplicitWaitModes<GpuCompletionPoint>);
        EXPECT_TRUE(SupportsTimelineWaitModes<Semaphore>);
        EXPECT_TRUE(std::is_copy_constructible_v<GpuCompletionPoint>);
        EXPECT_TRUE(std::is_copy_assignable_v<GpuCompletionPoint>);
    }
}
