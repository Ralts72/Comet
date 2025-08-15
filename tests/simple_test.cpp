#include <gtest/gtest.h>
#include "common/math_utils.h"
#include "common/log_system/log_system.h"

namespace Comet::Tests {

// 简单的数学测试
TEST(SimpleMathTest, BasicMathOperations) {
    Vec3 v1(1.0f, 2.0f, 3.0f);
    Vec3 v2(4.0f, 5.0f, 6.0f);
    
    Vec3 result = v1 + v2;
    EXPECT_FLOAT_EQ(result.x, 5.0f);
    EXPECT_FLOAT_EQ(result.y, 7.0f);
    EXPECT_FLOAT_EQ(result.z, 9.0f);
}

// 简单的日志系统测试
TEST(SimpleLogTest, LogSystemWorks) {
    LogSystem& logSystem = LogSystem::instance();
    EXPECT_NE(logSystem.m_console_logger, nullptr);
    
    // 测试日志输出不崩溃
    EXPECT_NO_THROW({
        LOG_INFO("Test log message");
        LOG_DEBUG("Debug message");
        LOG_WARN("Warning message");
    });
}

} // namespace Comet::Tests