#include "core/timer.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(TimerTest, StartsWithEmptyUpdateContext) {
        const Timer timer;
        const UpdateContext context = timer.get_update_context();
        EXPECT_FLOAT_EQ(context.delta_time, 0.0f);
        EXPECT_FLOAT_EQ(context.total_time, 0.0f);
        EXPECT_EQ(context.frame_index, 0);
        EXPECT_FLOAT_EQ(context.fps, 0.0f);
    }

    TEST(TimerTest, AdvancesFramesAndAccumulatesNonnegativeElapsedTime) {
        Timer timer;
        float previous_total = 0.0f;
        for(int frame = 1; frame <= 10; ++frame) {
            timer.tick();
            const UpdateContext context = timer.get_update_context();
            EXPECT_EQ(context.frame_index, frame);
            EXPECT_GE(context.delta_time, 0.0f);
            EXPECT_GE(context.total_time, previous_total);
            EXPECT_FLOAT_EQ(context.total_time, previous_total + context.delta_time);
            EXPECT_GE(context.fps, 0.0f);
            previous_total = context.total_time;
        }
    }
}
