#include <gtest/gtest.h>

#include "graphics/resource/allocator.h"

#include <limits>
#include <type_traits>

namespace Comet::Tests {
    static_assert(!std::is_default_constructible_v<GpuResourceResult<int>>);
    static_assert(!std::is_default_constructible_v<GpuResourceResult<void>>);

    TEST(AllocationTest, DefaultsToInvalidHandle) {
        const Allocation allocation;

        EXPECT_FALSE(static_cast<bool>(allocation));
    }

    TEST(GpuResourceResultTest, DistinguishesSuccessFromFailure) {
        const auto failure =
            GpuResourceResult<int>::failure(vk::Result::eErrorOutOfDeviceMemory);
        const auto success = GpuResourceResult<int>::success(42);
        const auto normalized_failure =
            GpuResourceResult<int>::failure(vk::Result::eSuccess);

        EXPECT_FALSE(static_cast<bool>(failure));
        EXPECT_EQ(failure.result(), vk::Result::eErrorOutOfDeviceMemory);
        EXPECT_TRUE(static_cast<bool>(success));
        EXPECT_EQ(success.value(), 42);
        EXPECT_FALSE(static_cast<bool>(normalized_failure));
        EXPECT_EQ(normalized_failure.result(), vk::Result::eErrorUnknown);

        const auto empty_failure =
            GpuResourceResult<void>::failure(vk::Result::eErrorOutOfDeviceMemory);
        const auto empty_success = GpuResourceResult<void>::success();
        EXPECT_FALSE(static_cast<bool>(empty_failure));
        EXPECT_TRUE(static_cast<bool>(empty_success));
    }

    TEST(GpuResourceResultTest, RejectsFailedValueAccess) {
        EXPECT_DEATH(
            {
                auto failure =
                    GpuResourceResult<int>::failure(vk::Result::eErrorOutOfDeviceMemory);
                static_cast<void>(failure.value());
            },
            "");
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
        EXPECT_TRUE(
            budget.reaches_usage_percentage(std::numeric_limits<uint64_t>::max(), 90));
        EXPECT_FALSE(budget.reaches_usage_percentage(1, 0));
        EXPECT_FALSE(budget.reaches_usage_percentage(1, 101));

        budget.budget_bytes = 0;
        EXPECT_FALSE(budget.reaches_usage_percentage(1, 90));
    }

}
