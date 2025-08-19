#include <gtest/gtest.h>
#include "../engine/src/core/math_utils.h"

namespace Comet::Tests {

// 简单的数学测试
TEST(SimpleMathTest, BasicMathOperations) {
    Math::Vec3 v1(1.0f, 2.0f, 3.0f);
    Math::Vec3 v2(4.0f, 5.0f, 6.0f);
    
    Math::Vec3 result = v1 + v2;
    EXPECT_FLOAT_EQ(result.x, 5.0f);
    EXPECT_FLOAT_EQ(result.y, 7.0f);
    EXPECT_FLOAT_EQ(result.z, 9.0f);
}

} // namespace Comet::Tests