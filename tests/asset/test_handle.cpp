#include <gtest/gtest.h>

#include "asset/handle.h"

#include <cstdint>
#include <type_traits>

namespace Comet::Tests {
    static_assert(std::is_trivially_copyable_v<AssetHandle>);
    static_assert(!std::is_convertible_v<std::uint64_t, AssetHandle>);
    static_assert(!std::is_convertible_v<AssetHandle, std::uint64_t>);

    static_assert(!AssetHandle{} && !AssetHandle(0) && !INVALID_ASSET_HANDLE);
    static_assert(AssetHandle{} == INVALID_ASSET_HANDLE);
    static_assert(AssetHandle(0).value() == 0);
    static_assert(AssetHandle(42).is_valid() && AssetHandle(42).value() == 42);
    static_assert(AssetHandle(1) == AssetHandle(1));
    static_assert(AssetHandle(1) != AssetHandle(2) && AssetHandle(1) < AssetHandle(2));

    TEST(AssetHandleTest, GeneratesValidPersistentValue) {
        const AssetHandle first = AssetHandle::generate();
        const AssetHandle second = AssetHandle::generate();

        EXPECT_TRUE(first);
        EXPECT_TRUE(second);
        EXPECT_NE(first, second);
    }

    TEST(AssetHandleTest, ParsesCanonicalDecimalIdentitiesWithoutPrecisionLoss) {
        EXPECT_EQ(AssetHandle::parse("1"), AssetHandle(1));
        EXPECT_EQ(AssetHandle::parse("18446744073709551615"), AssetHandle(UINT64_MAX));
        for(const auto* text :
            {"", "0", "01", "+1", "-1", " 1", "1 ", "1.0", "1e2", "18446744073709551616"}) {
            SCOPED_TRACE(text);
            EXPECT_FALSE(AssetHandle::parse(text));
        }
        EXPECT_FALSE(AssetHandle::parse(std::string_view("1\0hidden", 8)));
    }
}
