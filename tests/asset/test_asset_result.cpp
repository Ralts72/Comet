#include "asset/result.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace Comet::Tests {
    TEST(AssetResultTest, DistinguishesStringValueFromErrorIncludingEmptyMessages) {
        const auto value = AssetResult<std::string>::success("asset contents");
        const auto error = AssetResult<std::string>::failure("cannot read asset");
        ASSERT_TRUE(value);
        ASSERT_FALSE(error);
        EXPECT_EQ(value.value(), "asset contents");
        EXPECT_EQ(error.error(), "cannot read asset");
        EXPECT_FALSE(AssetResult<std::string>::failure(""));
        EXPECT_TRUE(AssetResult<void>::success());
        EXPECT_FALSE(AssetResult<void>::failure(""));
    }

    TEST(AssetResultTest, TransfersMoveOnlyPayloadWithoutCopying) {
        auto result =
            AssetResult<std::unique_ptr<int>>::success(std::make_unique<int>(42));
        ASSERT_TRUE(result);
        auto value = std::move(result).value();
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(*value, 42);
    }
}
