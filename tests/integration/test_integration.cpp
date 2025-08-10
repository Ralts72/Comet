#include <gtest/gtest.h>
#include "common/log_system/log_system.h"
#include "common/math_utils.h"

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
    Mat4 projMatrix = perspective(45.0f, aspect, 0.1f, 100.0f);

    // 验证投影矩阵不为零
    EXPECT_FALSE(projMatrix == Mat4(0.0f));

    // 测试变换矩阵链
    Mat4 modelMatrix = translate(Vec3(0.0f, 0.0f, -5.0f)) *
                      rotate(PI/4.0f, Vec3(0.0f, 1.0f, 0.0f)) *
                      scale(Vec3(1.0f, 1.0f, 1.0f));

    Mat4 mvpMatrix = projMatrix * modelMatrix;
    EXPECT_FALSE(mvpMatrix == Mat4(0.0f));
}

TEST_F(IntegrationTest, LoggingSystemTest) {
    // 测试日志系统
    LogSystem& logSystem = LogSystem::instance();
    EXPECT_NE(logSystem.m_console_logger, nullptr);

    LOG_INFO("Integration test logging");
    LOG_DEBUG("Debug message");
    LOG_WARN("Warning message");

    // 如果到这里没有崩溃，说明日志系统工作正常
    SUCCEED();
}

} // namespace Comet::Tests
