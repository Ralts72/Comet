#include "core/task_scheduler.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>

namespace Comet::Tests {
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
