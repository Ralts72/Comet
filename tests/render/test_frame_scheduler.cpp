#include "render/frame_scheduler.h"

#include <concepts>
#include <cstdint>
#include <gtest/gtest.h>
#include <type_traits>

namespace Comet::Tests {
    namespace {
        template<typename T>
        concept HasFrameLifecycleContract = requires(
            T& scheduler,
            const GpuCompletionPoint& completion) {
            scheduler.wait_for_current_slot();
            scheduler.begin_frame(0);
            scheduler.record_submission(completion);
            scheduler.end_frame();
            {
                scheduler.get_current_frame_serial()
            } -> std::same_as<uint64_t>;
        };
    }

    TEST(FrameSchedulerInterfaceTest, ExposesOrderedFrameLifecycle) {
        EXPECT_TRUE(HasFrameLifecycleContract<FrameScheduler>);
        EXPECT_FALSE(std::is_copy_constructible_v<FrameScheduler>);
        EXPECT_FALSE(std::is_copy_assignable_v<FrameScheduler>);
        EXPECT_FALSE(std::is_move_constructible_v<FrameScheduler>);
        EXPECT_FALSE(std::is_move_assignable_v<FrameScheduler>);
    }
}
