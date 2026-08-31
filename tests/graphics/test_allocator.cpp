#include <gtest/gtest.h>

#include "graphics/resource/allocator.h"
#include "graphics/resource/buffer.h"

#include <concepts>
#include <type_traits>

namespace Comet::Tests {
    namespace {
        template<typename T>
        concept SupportsRangeWrite = requires(
            const T& buffer,
            const void* data,
            const size_t size,
            const size_t offset) {
            buffer.write(data, size, offset);
        };

        template<typename T>
        concept SupportsFrameIndex = requires(
            const T& allocator,
            const uint64_t frame_serial) {
            allocator.set_current_frame_index(frame_serial);
            {
                allocator.is_memory_budget_enabled()
            } -> std::same_as<bool>;
        };
    }

    TEST(AllocationTest, DefaultsToInvalidHandle) {
        const Allocation allocation;

        EXPECT_FALSE(static_cast<bool>(allocation));
    }

    TEST(AllocationTest, TransfersWithoutCopying) {
        EXPECT_FALSE(std::is_copy_constructible_v<Allocation>);
        EXPECT_FALSE(std::is_copy_assignable_v<Allocation>);
        EXPECT_TRUE(std::is_move_constructible_v<Allocation>);
        EXPECT_TRUE(std::is_move_assignable_v<Allocation>);
    }

    TEST(AllocationTest, CreateInfoDefaultsToDeviceLocalIntent) {
        const AllocationCreateInfo create_info;

        EXPECT_EQ(create_info.usage, AllocationUsage::Device);
        EXPECT_FALSE(create_info.persistent_mapping);
        EXPECT_TRUE(create_info.debug_name.empty());
    }

    TEST(CPUBufferInterfaceTest, SupportsBoundedRangeWrites) {
        EXPECT_TRUE(SupportsRangeWrite<CPUBuffer>);
    }

    TEST(AllocatorInterfaceTest, MemoryBudgetIsOptional) {
        const Allocator::CreateInfo create_info;

        EXPECT_FALSE(create_info.memory_budget_enabled);
        EXPECT_TRUE(SupportsFrameIndex<Allocator>);
    }
}
