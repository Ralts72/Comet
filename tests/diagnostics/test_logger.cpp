#include <gtest/gtest.h>
#include "diagnostics/logger.h"

#include <spdlog/sinks/callback_sink.h>

#include <string>
#include <vector>

using namespace Comet;

class LoggerTest : public ::testing::Test {
protected:
    void TearDown() override {
        Logger::shutdown();
    }
};

TEST_F(LoggerTest, InitUsesExplicitConfig) {
    Logger::shutdown();

    Config::Log config;
    config.enable_file_logging = false;
    config.level = "error";

    Logger::init(config);

    auto logger = Logger::get_console_logger();
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->level(), spdlog::level::err);
    EXPECT_TRUE(Logger::get_log_file_path().empty());
}

TEST_F(LoggerTest, GettersDoNotImplicitlyInitializeAfterShutdown) {
    Config::Log config;
    config.enable_file_logging = false;
    config.level = "info";

    Logger::init(config);
    ASSERT_NE(Logger::get_console_logger(), nullptr);

    Logger::shutdown();

    EXPECT_EQ(Logger::get_console_logger(), nullptr);
    EXPECT_EQ(Logger::get_profiler_logger(), nullptr);
    EXPECT_TRUE(Logger::get_log_file_path().empty());
}

TEST_F(LoggerTest, CreatesProfilerLoggerOnlyWhenRuntimeProfilingIsEnabled) {
    Logger::shutdown();

    Config::Log config;
    config.enable_file_logging = false;

    Logger::init(config, false);
    EXPECT_EQ(Logger::get_profiler_logger(), nullptr);

    Logger::shutdown();
    Logger::init(config, true);
#ifdef COMET_ENABLE_PROFILER
    EXPECT_NE(Logger::get_profiler_logger(), nullptr);
#else
    EXPECT_EQ(Logger::get_profiler_logger(), nullptr);
#endif
}

TEST_F(LoggerTest, OperationalMacrosRemainAvailableInOptimizedBuilds) {
    Logger::shutdown();

    Config::Log config;
    config.enable_file_logging = false;
    config.level = "info";
    Logger::init(config);

    std::vector<std::string> messages;
    const auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
        [&messages](const spdlog::details::log_msg& message) {
            messages.emplace_back(message.payload.data(), message.payload.size());
        });
    Logger::add_custom_sink(sink);

    LOG_INFO("info message");
    LOG_WARN("warning message");
    LOG_ERROR("error message");

    EXPECT_EQ(messages, (std::vector<std::string>{
        "info message",
        "warning message",
        "error message"
    }));
}
