#include "graphics/synchronization/gpu_retirement_queue.h"

#include <gtest/gtest.h>
#include <memory>
#include <type_traits>

namespace Comet::Tests {
    TEST(GpuRetirementQueueTest, EmptyQueueHasNoPendingOwnership) {
        GpuRetirementQueue queue;

        queue.collect_completed();
        queue.retire_batch({}, {});

        EXPECT_EQ(queue.get_pending_batch_count(), 0U);
        EXPECT_EQ(queue.get_pending_resource_count(), 0U);
    }

    TEST(GpuRetirementQueueTest, HasSingleOwnerThreadSemantics) {
        EXPECT_FALSE(std::is_copy_constructible_v<GpuRetirementQueue>);
        EXPECT_FALSE(std::is_copy_assignable_v<GpuRetirementQueue>);
        EXPECT_FALSE(std::is_move_constructible_v<GpuRetirementQueue>);
        EXPECT_FALSE(std::is_move_assignable_v<GpuRetirementQueue>);

        EXPECT_TRUE((std::is_invocable_v<
            decltype(&GpuRetirementQueue::retire<int>),
            GpuRetirementQueue&,
            const GpuCompletionPoint&,
            std::shared_ptr<int>>));
    }
}
