#include <gtest/gtest.h>
#include "../engine/src/core/math_utils.h"
#include "core/logger/logger.h"
#include <spdlog/sinks/stdout_color_sinks.h>

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

// 简单的日志系统测试
TEST(SimpleLogTest, LogSystemWorks) {
    // 创建测试专用logger
    auto test_logger = spdlog::stdout_color_mt("simple_test_logger");
    Logger::set_test_logger(test_logger);
    
    auto logger = Logger::get_console_logger();
    EXPECT_NE(logger, nullptr);
    
    // 测试日志输出不崩溃
    EXPECT_NO_THROW({
        LOG_INFO("Test log message");
        LOG_DEBUG("Debug message");
        LOG_WARN("Warning message");
    });
    
    Logger::shutdown();
}

} // namespace Comet::Tests