#include "diagnostics/timing_history.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    namespace {
        using namespace std::chrono_literals;
        constexpr auto start = TimingHistory::Clock::time_point(10s);
    }

    TEST(TimingHistoryTest, WeightsEverySampleAndRetainsPeaksBetweenDisplayRefreshes) {
        TimingHistory history;
        const TimingHistory::Entry normal[]{{"scene", 1}};
        for(int sample = 0; sample < 5000; ++sample)
            history.record(2, normal, start);
        const TimingHistory::Entry spike[]{{"scene", 90}};
        history.record(100, spike, start + 50ms);
        const auto summary = history.summarize(start + 250ms);
        EXPECT_EQ(summary.total.count, 5001);
        EXPECT_DOUBLE_EQ(summary.total.average(), 10100.0 / 5001);
        EXPECT_DOUBLE_EQ(summary.total.maximum, 100);
        ASSERT_EQ(summary.details.size(), 1);
        EXPECT_DOUBLE_EQ(summary.details[0].timing.maximum, 90);
    }

    TEST(TimingHistoryTest, WindowExpiresWhileTrendRetainsFiveSecondsWithoutInventingSamples) {
        TimingHistory history;
        history.record(8, {}, start);
        history.record(3, {}, start + 1s);
        auto summary = history.summarize(start + 1s);
        EXPECT_EQ(summary.total.count, 1);
        EXPECT_DOUBLE_EQ(summary.total.maximum, 3);
        size_t points = 0;
        for(const auto& point : summary.trend)
            points += point.count;
        EXPECT_EQ(points, 2);
        EXPECT_EQ(history.summarize(start + 6s).total.count, 0);
        for(const auto& point : history.summarize(start + 6s).trend)
            EXPECT_EQ(point.count, 0);
        history.record(4, {}, start + 6s);
        EXPECT_EQ(history.summarize(start + 6s).total.count, 1);
    }

    TEST(TimingHistoryTest, LayoutChangesAndClearDoNotMixDifferentPasses) {
        TimingHistory history;
        const TimingHistory::Entry old_passes[]{{"scene", 2}, {"bloom", 1}};
        const TimingHistory::Entry new_passes[]{{"scene", 4}};
        history.record(3, old_passes, start);
        history.record(4, new_passes, start + 50ms);
        const auto summary = history.summarize(start + 50ms);
        EXPECT_EQ(summary.total.count, 1);
        ASSERT_EQ(summary.details.size(), 1);
        EXPECT_EQ(summary.details.front().name, "scene");
        history.record(999, new_passes, start);
        EXPECT_EQ(history.summarize(start + 50ms).total.count, 1);
        history.clear();
        EXPECT_FALSE(history.last_sample_time());
        EXPECT_EQ(history.summarize(start + 50ms).total.count, 0);
    }
}
