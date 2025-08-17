#include <gtest/gtest.h>
#include "core/logger/logger.h"
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
#include <memory>

namespace Comet::Tests {

class LogSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建一个字符串流来捕获日志输出
        log_stream = std::make_shared<std::ostringstream>();

        // 创建一个输出到字符串流的sink
        auto ostream_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(*log_stream);

        // 创建测试logger
        test_logger = std::make_shared<spdlog::logger>("test_logger", ostream_sink);
        test_logger->set_level(spdlog::level::trace);
        test_logger->set_pattern("[%l] %v");
        
        // 设置测试logger
        Logger::set_test_logger(test_logger);
    }

    void TearDown() override {
        log_stream->str("");
        log_stream->clear();
        Logger::shutdown();
    }

    std::string getLogOutput() {
        return log_stream->str();
    }

    bool containsLogLevel(const std::string& level) {
        std::string output = getLogOutput();
        return output.find("[" + level + "]") != std::string::npos;
    }

    bool containsMessage(const std::string& message) {
        std::string output = getLogOutput();
        return output.find(message) != std::string::npos;
    }

private:
    std::shared_ptr<std::ostringstream> log_stream;
    std::shared_ptr<spdlog::logger> test_logger;
};

TEST_F(LogSystemTest, LogSystemInitialization) {
    // 测试日志系统初始化
    auto logger = Logger::get_console_logger();
    EXPECT_NE(logger, nullptr);
    EXPECT_EQ(logger->name(), "test_logger");
}

TEST_F(LogSystemTest, LogError) {
    LOG_ERROR("This is an error message");

    EXPECT_TRUE(containsLogLevel("error"));
    EXPECT_TRUE(containsMessage("This is an error message"));
}

TEST_F(LogSystemTest, LogWarn) {
    LOG_WARN("This is a warning message");

    EXPECT_TRUE(containsLogLevel("warning"));
    EXPECT_TRUE(containsMessage("This is a warning message"));
}

TEST_F(LogSystemTest, LogInfo) {
    LOG_INFO("This is an info message");

    EXPECT_TRUE(containsLogLevel("info"));
    EXPECT_TRUE(containsMessage("This is an info message"));
}

TEST_F(LogSystemTest, LogDebug) {
    LOG_DEBUG("This is a debug message");

    EXPECT_TRUE(containsLogLevel("debug"));
    EXPECT_TRUE(containsMessage("This is a debug message"));
}

TEST_F(LogSystemTest, LogTrace) {
    LOG_TRACE("This is a trace message");

    EXPECT_TRUE(containsLogLevel("trace"));
    EXPECT_TRUE(containsMessage("This is a trace message"));
}

TEST_F(LogSystemTest, LogWithParameters) {
    std::string name = "TestUser";
    int value = 42;

    LOG_INFO("User {} has value {}", name, value);

    EXPECT_TRUE(containsLogLevel("info"));
    EXPECT_TRUE(containsMessage("User TestUser has value 42"));
}

TEST_F(LogSystemTest, LogWithDifferentTypes) {
    float pi = 3.14159f;
    bool flag = true;

    LOG_DEBUG("Pi is approximately {:.2f}, flag is {}", pi, flag);

    EXPECT_TRUE(containsLogLevel("debug"));
    EXPECT_TRUE(containsMessage("Pi is approximately 3.14"));
    EXPECT_TRUE(containsMessage("flag is true"));
}

TEST_F(LogSystemTest, MultipleLogCalls) {
    LOG_INFO("First message");
    LOG_WARN("Second message");
    LOG_ERROR("Third message");

    std::string output = getLogOutput();

    EXPECT_TRUE(containsMessage("First message"));
    EXPECT_TRUE(containsMessage("Second message"));
    EXPECT_TRUE(containsMessage("Third message"));

    // 检查消息的顺序
    size_t first_pos = output.find("First message");
    size_t second_pos = output.find("Second message");
    size_t third_pos = output.find("Third message");

    EXPECT_LT(first_pos, second_pos);
    EXPECT_LT(second_pos, third_pos);
}

TEST_F(LogSystemTest, LoggerConfiguration) {
    auto logger = Logger::get_console_logger();

    // 测试logger是否正确配置
    EXPECT_NE(logger, nullptr);
    // 注意：由于LogSystem使用全局logger，这里的名称可能不同
}

// 注意：LOG_FATAL测试被跳过，因为它会调用assert(false)导致程序终止
// 在实际应用中，可以通过模拟或特殊的测试环境来测试这种情况

TEST_F(LogSystemTest, LogMacroSafety) {
    // 测试宏在条件语句中的安全使用
    bool condition = true;

    if (condition)
        LOG_INFO("Conditional log message");
    else
        LOG_ERROR("This should not appear");

    EXPECT_TRUE(containsMessage("Conditional log message"));
    EXPECT_FALSE(containsMessage("This should not appear"));
}

TEST_F(LogSystemTest, LogWithSpecialCharacters) {
    LOG_INFO("Special characters: !@#$%^&*()_+-=[]{}|;':\",./<>?");

    EXPECT_TRUE(containsMessage("Special characters: !@#$%^&*()_+-=[]{}|;':\",./<>?"));
}

TEST_F(LogSystemTest, LogEmptyMessage) {
    LOG_INFO("");

    EXPECT_TRUE(containsLogLevel("info"));
}

TEST_F(LogSystemTest, LogLongMessage) {
    std::string long_message(1000, 'A');
    LOG_INFO("Long message: {}", long_message);

    EXPECT_TRUE(containsMessage("Long message:"));
    EXPECT_TRUE(containsMessage(long_message));
}

} // namespace Comet::Tests
