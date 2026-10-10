#include "core/frame_pacer.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(FramePacerTest, UnlimitedAndInvalidLimitsDoNotIntroduceADeadline) {
        FramePacer pacer;
        const FramePacer::Clock::time_point start{};
        EXPECT_FALSE(pacer.deadline(start));
        EXPECT_FALSE(pacer.set_limit(-1));
        EXPECT_FALSE(pacer.set_limit(FramePacer::MAX_LIMIT + 1));
        EXPECT_EQ(pacer.limit(), 0);
        ASSERT_TRUE(pacer.set_limit(60));
        EXPECT_FALSE(pacer.set_limit(-1));
        EXPECT_EQ(pacer.limit(), 60);
        ASSERT_TRUE(pacer.set_limit(0));
        EXPECT_FALSE(pacer.deadline(start));
    }

    TEST(FramePacerTest, DeadlinesUseTheCurrentFrameWithoutAccumulatingCatchupOrExtraVsyncWait) {
        FramePacer pacer;
        ASSERT_TRUE(pacer.set_limit(60));
        const FramePacer::Clock::time_point start{};
        const auto deadline = pacer.deadline(start);
        ASSERT_TRUE(deadline);
        EXPECT_NEAR(std::chrono::duration<double>(*deadline - start).count(), 1.0 / 60, 1e-8);
        const auto gpu_finished = start + std::chrono::milliseconds(20);
        EXPECT_LT(*deadline, gpu_finished);
        const auto next = pacer.deadline(gpu_finished);
        ASSERT_TRUE(next);
        EXPECT_EQ(*next - gpu_finished, *deadline - start);
        ASSERT_TRUE(pacer.set_limit(120));
        const auto high_refresh_deadline = pacer.deadline(gpu_finished);
        ASSERT_TRUE(high_refresh_deadline);
        EXPECT_NEAR(std::chrono::duration<double>(*high_refresh_deadline - gpu_finished).count(),
            1.0 / 120, 1e-8);
        EXPECT_LT(*high_refresh_deadline, *next);
        ASSERT_TRUE(pacer.set_limit(1000));
        EXPECT_EQ(*pacer.deadline(start) - start, std::chrono::milliseconds(1));
    }
}
