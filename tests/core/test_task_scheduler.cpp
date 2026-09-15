#include "core/task_scheduler.h"
#include "support/blocked_worker.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>

namespace Comet::Tests {
    TEST(TaskSchedulerTest, RejectsFullQueueWithoutBlockingOrRunningOnCaller) {
        std::atomic<int> executed = 0;
        TaskScheduler scheduler(1, 1);
        BlockedWorker blocker(scheduler);
        auto accepted = scheduler.try_submit([&] { ++executed; });
        ASSERT_TRUE(accepted);
        EXPECT_FALSE(scheduler.try_submit([&] { ++executed; }));
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
        std::atomic<int> accepted = 0;
        std::atomic<int> executed = 0;
        TaskScheduler scheduler(1, 4);
        BlockedWorker blocker(scheduler);
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
        const auto previous_style = GTEST_FLAG_GET(death_test_style);
        GTEST_FLAG_SET(death_test_style, "threadsafe");
        EXPECT_DEATH((TaskScheduler{1, 0}), "");
        GTEST_FLAG_SET(death_test_style, previous_style);
        TaskScheduler scheduler(1, 1);
        EXPECT_FALSE(scheduler.try_submit({}));
    }

    TEST(TaskSchedulerTest, ExecutesSubmittedTasksAndWaitsUntilIdle) {
        TaskScheduler scheduler(2);
        std::atomic<int> completed_tasks = 0;

        auto first = scheduler.try_submit([&] { ++completed_tasks; });
        auto second = scheduler.try_submit([&] { ++completed_tasks; });
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);

        scheduler.wait_idle();

        EXPECT_EQ(completed_tasks.load(), 2);
        EXPECT_EQ(first->wait_for(std::chrono::seconds(0)), std::future_status::ready);
        EXPECT_EQ(second->wait_for(std::chrono::seconds(0)), std::future_status::ready);
    }

    TEST(TaskSchedulerTest, DeliversTaskExceptionsThroughFuture) {
        TaskScheduler scheduler(1);
        auto result = scheduler.try_submit([] { throw std::runtime_error("task failed"); });
        ASSERT_TRUE(result);

        EXPECT_THROW(result->get(), std::runtime_error);
        scheduler.wait_idle();
    }

    TEST(TaskSchedulerTest, ShutdownDrainsAcceptedTasksAndRejectsNewWork) {
        TaskScheduler scheduler(1);
        std::atomic<int> completed = 0;
        std::vector<std::future<void>> accepted;
        for(int i = 0; i < 16; ++i) {
            auto task = scheduler.try_submit([&] { ++completed; });
            ASSERT_TRUE(task);
            accepted.push_back(std::move(*task));
        }

        scheduler.shutdown();
        EXPECT_EQ(completed.load(), 16);
        for(auto& task : accepted) {
            EXPECT_EQ(task.wait_for(std::chrono::seconds(0)), std::future_status::ready);
            EXPECT_NO_THROW(task.get());
        }
        EXPECT_FALSE(scheduler.try_submit([&] { ++completed; }));
        scheduler.shutdown();
        scheduler.wait_idle();
        EXPECT_EQ(completed.load(), 16);
    }

    TEST(TaskSchedulerTest, DrainsQueuedTasksDuringDestruction) {
        std::atomic<int> completed_tasks = 0;
        {
            TaskScheduler scheduler(1);
            ASSERT_TRUE(scheduler.try_submit([&] { ++completed_tasks; }));
            ASSERT_TRUE(scheduler.try_submit([&] { ++completed_tasks; }));
        }

        EXPECT_EQ(completed_tasks.load(), 2);
    }
}
