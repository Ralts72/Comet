#include <gtest/gtest.h>
#include "common/logger.h"

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
