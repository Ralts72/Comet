#include "render/frame_scheduler.h"

#include <concepts>
#include <cstdint>
#include <gtest/gtest.h>
#include <type_traits>

namespace Comet::Tests {
    namespace {
        template<typename T>
        concept HasFrameLifecycleContract = requires(T& scheduler) {
            scheduler.wait_for_current_slot();
            scheduler.wait_for_all_slots();
            scheduler.begin_frame(0);
            scheduler.record_submission();
            scheduler.end_frame();
            {
                scheduler.get_current_frame_serial()
            } -> std::same_as<uint64_t>;
            {
                scheduler.get_completed_frame_serial()
            } -> std::same_as<uint64_t>;
            {
                scheduler.is_frame_serial_complete(1)
            } -> std::same_as<bool>;
        };

        template<typename T>
        concept StoresTimelineCompletion = requires(T& slot) {
            slot.last_submission;
        };
    }

    TEST(FrameSchedulerInterfaceTest, ExposesOrderedFrameLifecycle) {
        EXPECT_TRUE(HasFrameLifecycleContract<FrameScheduler>);
        EXPECT_FALSE(std::is_copy_constructible_v<FrameScheduler>);
        EXPECT_FALSE(std::is_copy_assignable_v<FrameScheduler>);
        EXPECT_FALSE(std::is_move_constructible_v<FrameScheduler>);
        EXPECT_FALSE(std::is_move_assignable_v<FrameScheduler>);
        EXPECT_FALSE(StoresTimelineCompletion<FrameSlot>);
    }
}
