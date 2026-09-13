#include <gtest/gtest.h>

#include "graphics/resource/allocator.h"

#include <limits>

namespace Comet::Tests {
    TEST(AllocationTest, DefaultsToInvalidHandle) {
        const Allocation allocation;

        EXPECT_FALSE(static_cast<bool>(allocation));
    }

    TEST(MemoryHeapBudgetTest, AvailableBytesSaturatesAtZero) {
        MemoryHeapBudget budget;
        budget.usage_bytes = 70;
        budget.budget_bytes = 100;
        EXPECT_EQ(budget.available_bytes(), 30U);

        budget.usage_bytes = 120;
        EXPECT_EQ(budget.available_bytes(), 0U);
    }

    TEST(MemoryHeapBudgetTest, ProjectsUsageAgainstPercentageWithoutOverflow) {
        MemoryHeapBudget budget;
        budget.usage_bytes = 89;
        budget.budget_bytes = 100;

        EXPECT_FALSE(budget.reaches_usage_percentage(0, 90));
        EXPECT_TRUE(budget.reaches_usage_percentage(1, 90));
        EXPECT_TRUE(budget.reaches_usage_percentage(std::numeric_limits<uint64_t>::max(), 90));
        EXPECT_FALSE(budget.reaches_usage_percentage(1, 0));
        EXPECT_FALSE(budget.reaches_usage_percentage(1, 101));

        budget.budget_bytes = 0;
        EXPECT_FALSE(budget.reaches_usage_percentage(1, 90));
    }

}
