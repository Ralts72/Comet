#include "common/retry_backoff.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    using namespace std::chrono_literals;

    TEST(RetryBackoffTest, ConsumesEachDeadlineOnceAndStopsAtLimit) {
        RetryBackoff retry;
        RetryBackoff::Clock::time_point now{};
        EXPECT_FALSE(retry.consume(now));
        for(uint32_t index = 0; index < 3; ++index) {
            ASSERT_TRUE(retry.schedule(now));
            EXPECT_TRUE(retry.schedule(now + 500ms));
            EXPECT_EQ(retry.retry_count(), index + 1);
            const auto due = now + std::chrono::seconds(1 << index);
            EXPECT_FALSE(retry.consume(due - 1ms));
            ASSERT_TRUE(retry.consume(due));
            EXPECT_FALSE(retry.consume(due));
            now = due;
        }
        EXPECT_FALSE(retry.schedule(now));
        EXPECT_FALSE(retry.consume(now + 1h));
    }

    TEST(RetryBackoffTest, ResetCancelsDeadlineAndRestoresBudget) {
        RetryBackoff retry;
        const RetryBackoff::Clock::time_point now{};
        ASSERT_TRUE(retry.schedule(now));
        retry.reset();
        EXPECT_FALSE(retry.consume(now + 1h));
        EXPECT_EQ(retry.retry_count(), 0u);
        for(int index = 0; index < 3; ++index) {
            ASSERT_TRUE(retry.schedule(now));
            ASSERT_TRUE(retry.consume(now + 1h));
        }
        EXPECT_FALSE(retry.schedule(now));
        retry.reset();
        ASSERT_TRUE(retry.schedule(now));
        EXPECT_FALSE(retry.consume(now + 999ms));
        EXPECT_TRUE(retry.consume(now + 1s));
        EXPECT_FALSE(retry.consume(now + 1h));
    }

    TEST(RetryBackoffTest, OwnsPolicyAndCapsDelayWithoutExtendingBudget) {
        RetryBackoff retry({.initial_delay = 100ms, .max_delay = 250ms, .max_retries = 5});
        RetryBackoff::Clock::time_point now{};
        for(const auto delay : {100ms, 200ms, 250ms, 250ms, 250ms}) {
            ASSERT_TRUE(retry.schedule(now));
            EXPECT_FALSE(retry.consume(now + delay - 1ms));
            now += delay;
            ASSERT_TRUE(retry.consume(now));
        }
        EXPECT_FALSE(retry.schedule(now));
        RetryBackoff disabled({.max_retries = 0});
        EXPECT_FALSE(disabled.schedule(now));
        EXPECT_FALSE(disabled.consume(now + 1h));
    }
}
