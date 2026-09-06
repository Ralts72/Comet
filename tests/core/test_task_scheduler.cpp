#include "core/task_scheduler.h"
#include "core/task_scheduler_test_utils.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>

namespace Comet::Tests {
    TEST(TaskSchedulerTest, RejectsFullQueueWithoutBlockingOrRunningOnCaller) {
        TaskScheduler scheduler(1, 1);
        BlockedWorker blocker(scheduler);
        std::atomic<int> executed = 0;
        auto accepted = scheduler.try_submit([&] { ++executed; });
        ASSERT_TRUE(accepted);
        EXPECT_FALSE(scheduler.try_submit([&] { ++executed; }));
        EXPECT_THROW(
            static_cast<void>(scheduler.submit([&] { ++executed; })), std::runtime_error);
        EXPECT_EQ(executed.load(), 0);
        EXPECT_EQ(scheduler.get_queue_capacity(), 1);
        blocker.release();
        accepted->get();
        scheduler.wait_idle();
        auto retried = scheduler.try_submit([&] { ++executed; });
        ASSERT_TRUE(retried);
        retried->get();
        EXPECT_EQ(executed.load(), 2);
    }

    TEST(TaskSchedulerTest, ConcurrentProducersRespectSingleQueueCapacity) {
        TaskScheduler scheduler(1, 4);
        BlockedWorker blocker(scheduler);
        std::atomic<int> accepted = 0;
        std::atomic<int> executed = 0;
        std::vector<std::thread> producers;
        for(int i = 0; i < 16; ++i)
            producers.emplace_back([&] {
                if(scheduler.try_submit([&] { ++executed; }))
                    ++accepted;
            });
        for(auto& producer : producers)
            producer.join();
        EXPECT_EQ(accepted.load(), 4);
        EXPECT_EQ(executed.load(), 0);
        blocker.release();
        scheduler.wait_idle();
        EXPECT_EQ(executed.load(), 4);
    }

    TEST(TaskSchedulerTest, RejectsZeroCapacityAndEmptyTask) {
        EXPECT_THROW((TaskScheduler{1, 0}), std::invalid_argument);
        TaskScheduler scheduler(1, 1);
        EXPECT_THROW(static_cast<void>(scheduler.try_submit({})), std::invalid_argument);
        EXPECT_THROW(static_cast<void>(scheduler.submit({})), std::invalid_argument);
    }

    TEST(TaskSchedulerTest, ExecutesSubmittedTasksAndWaitsUntilIdle) {
        TaskScheduler scheduler(2);
        std::atomic<int> completed_tasks = 0;

        std::future<void> first = scheduler.submit([&] { ++completed_tasks; });
        std::future<void> second = scheduler.submit([&] { ++completed_tasks; });

        scheduler.wait_idle();

        EXPECT_EQ(completed_tasks.load(), 2);
        EXPECT_EQ(first.wait_for(std::chrono::seconds(0)), std::future_status::ready);
        EXPECT_EQ(second.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    }

    TEST(TaskSchedulerTest, DeliversTaskExceptionsThroughFuture) {
        TaskScheduler scheduler(1);
        std::future<void> result =
            scheduler.submit([] { throw std::runtime_error("task failed"); });

        EXPECT_THROW(result.get(), std::runtime_error);
        scheduler.wait_idle();
    }

    TEST(TaskSchedulerTest, DrainsQueuedTasksDuringDestruction) {
        std::atomic<int> completed_tasks = 0;
        {
            TaskScheduler scheduler(1);
            static_cast<void>(scheduler.submit([&] { ++completed_tasks; }));
            static_cast<void>(scheduler.submit([&] { ++completed_tasks; }));
        }

        EXPECT_EQ(completed_tasks.load(), 2);
    }
}
