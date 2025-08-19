#include <gtest/gtest.h>
#include "../../engine/src/core/math_utils.h"

namespace Comet::Tests {

// 集成测试：测试多个模块之间的协作
class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 测试前准备
    }

    void TearDown() override {
        // 测试后清理
    }
};

TEST_F(IntegrationTest, MathLibraryBasicOperations) {
    // 测试数学库的基本运算
    float aspect = 16.0f / 9.0f;
    Math::Mat4 projMatrix = Math::perspective(45.0f, aspect, 0.1f, 100.0f);

    // 验证投影矩阵不为零
    EXPECT_FALSE(projMatrix == Math::Mat4(0.0f));

    // 测试变换矩阵链
    Math::Mat4 modelMatrix = Math::translate(Math::Mat4(1.0f),Math::Vec3(0.0f, 0.0f, -5.0f)) *
                      Math::rotate(Math::Mat4(1.0f),Math::PI/4.0f, Math::Vec3(0.0f, 1.0f, 0.0f)) *
                      Math::scale(Math::Mat4(1.0f),Math::Vec3(1.0f, 1.0f, 1.0f));

    Math::Mat4 mvpMatrix = projMatrix * modelMatrix;
    EXPECT_FALSE(mvpMatrix == Math::Mat4(0.0f));
}

} // namespace Comet::Tests
