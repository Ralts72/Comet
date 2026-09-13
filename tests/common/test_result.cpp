#include "common/result.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace Comet::Tests {
    TEST(ResultTest, DistinguishesStringValueFromErrorIncludingEmptyMessages) {
        const auto value = Result<std::string>::success("asset contents");
        const auto error = Result<std::string>::failure("cannot read asset");
        ASSERT_TRUE(value);
        ASSERT_FALSE(error);
        EXPECT_EQ(value.value(), "asset contents");
        EXPECT_EQ(error.error(), "cannot read asset");
        EXPECT_FALSE(Result<std::string>::failure(""));
        EXPECT_TRUE(Result<void>::success());
        EXPECT_FALSE(Result<void>::failure(""));
    }

    TEST(ResultTest, TransfersMoveOnlyPayloadWithoutCopying) {
        auto result = Result<std::unique_ptr<int>>::success(std::make_unique<int>(42));
        ASSERT_TRUE(result);
        auto value = std::move(result).value();
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(*value, 42);
    }
}
