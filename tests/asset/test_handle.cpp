#include <gtest/gtest.h>

#include "asset/handle.h"

#include <cstdint>
#include <type_traits>
#include <unordered_map>

namespace Comet::Tests {
    static_assert(std::is_trivially_copyable_v<AssetHandle>);
    static_assert(!std::is_convertible_v<std::uint64_t, AssetHandle>);
    static_assert(!std::is_convertible_v<AssetHandle, std::uint64_t>);

    TEST(AssetHandleTest, DefaultAndZeroValuesAreInvalid) {
        constexpr AssetHandle default_handle;
        constexpr AssetHandle zero_handle(0);

        EXPECT_FALSE(default_handle);
        EXPECT_FALSE(zero_handle);
        EXPECT_FALSE(INVALID_ASSET_HANDLE);
        EXPECT_EQ(default_handle, INVALID_ASSET_HANDLE);
        EXPECT_EQ(zero_handle.value(), 0u);
    }

    TEST(AssetHandleTest, PreservesValidUnderlyingValue) {
        constexpr AssetHandle handle(42);

        EXPECT_TRUE(handle);
        EXPECT_TRUE(handle.is_valid());
        EXPECT_EQ(handle.value(), 42u);
    }

    TEST(AssetHandleTest, GeneratesValidPersistentValue) {
        const AssetHandle first = AssetHandle::generate();
        const AssetHandle second = AssetHandle::generate();

        EXPECT_TRUE(first);
        EXPECT_TRUE(second);
        EXPECT_NE(first, second);
    }

    TEST(AssetHandleTest, SupportsValueComparison) {
        constexpr AssetHandle first(1);
        constexpr AssetHandle same_as_first(1);
        constexpr AssetHandle second(2);

        EXPECT_EQ(first, same_as_first);
        EXPECT_NE(first, second);
        EXPECT_LT(first, second);
    }

    TEST(AssetHandleTest, SupportsHashBasedLookup) {
        const AssetHandle mesh_handle(10);
        const AssetHandle material_handle(20);
        const std::unordered_map<AssetHandle, int> assets = {
            {mesh_handle, 1},
            {material_handle, 2}
        };

        EXPECT_EQ(assets.at(mesh_handle), 1);
        EXPECT_EQ(assets.at(material_handle), 2);
    }
}
