#include <gtest/gtest.h>
#include "config/config.h"
#include "diagnostics/logger.h"
#include "diagnostics/diagnostics.h"
#include "common/file_io.h"
#include "common/scope_exit.h"
#include "core/project_paths.h"
#include "support/temporary_directory.h"

#include <spdlog/sinks/callback_sink.h>

#include <string>
#include <vector>

using namespace Comet;

class LoggerTest: public ::testing::Test {
protected:
    void TearDown() override { Logger::shutdown(); }
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

    EXPECT_EQ(
        messages, (std::vector<std::string>{"info message", "warning message", "error message"}));
}

TEST_F(LoggerTest, OrdinaryAndProfilerFilesFollowEachProject) {
    Logger::shutdown();
    Tests::TemporaryDirectory directory;
    for(const auto* project : {"first", "second"}) {
        const ProjectPaths paths(directory.path() / project);
        Config::Diagnostics config;
        config.log.directory = paths.logs();
        config.enable_profiler = true;
        std::filesystem::path log_path;
        {
            Diagnostics diagnostics(config);
            log_path = Logger::get_log_file_path();
            EXPECT_EQ(log_path.parent_path(), paths.logs());
            LOG_INFO("project {}", project);
            if(const auto profiler = Logger::get_profiler_logger())
                profiler->info("profile {}", project);
        }
        const auto contents = read_text_file(log_path);
        ASSERT_TRUE(contents) << contents.error();
        EXPECT_NE(contents.value().find(std::string("project ") + project), std::string::npos);
        size_t profiler_files = 0;
        for(const auto& entry : std::filesystem::directory_iterator(paths.logs())) {
            if(!entry.path().filename().string().starts_with("profiler_"))
                continue;
            ++profiler_files;
            const auto profile = read_text_file(entry.path());
            ASSERT_TRUE(profile) << profile.error();
            EXPECT_NE(profile.value().find(std::string("profile ") + project), std::string::npos);
        }
#ifdef COMET_ENABLE_PROFILER
        EXPECT_EQ(profiler_files, 1u);
#else
        EXPECT_EQ(profiler_files, 0u);
#endif
        EXPECT_TRUE(Logger::get_log_file_path().empty());
    }
}

TEST_F(LoggerTest, MissingDirectoryOrDisabledFileLoggingDoesNotCreateFiles) {
    Logger::shutdown();
    Tests::TemporaryDirectory directory;
    const ScopeExit shutdown([] { Logger::shutdown(); });
    Config::Log config;
    Logger::init(config, true);
    EXPECT_NE(Logger::get_console_logger(), nullptr);
    EXPECT_TRUE(Logger::get_log_file_path().empty());
    Logger::shutdown();

    config.directory = directory.path() / "logs";
    config.enable_file_logging = false;
    Logger::init(config, true);
    EXPECT_TRUE(Logger::get_log_file_path().empty());
    EXPECT_FALSE(std::filesystem::exists(config.directory));
}

TEST_F(LoggerTest, UnwritableDirectoryKeepsConsoleLogging) {
    Logger::shutdown();
    Tests::TemporaryDirectory directory;
    const ScopeExit shutdown([] { Logger::shutdown(); });
    const auto blocked = directory.path() / "not-a-directory";
    ASSERT_TRUE(write_text_file_atomic(blocked, "preserved"));
    Config::Log config;
    config.directory = blocked / "logs";
    ::testing::internal::CaptureStderr();
    Logger::init(config, true);
    const auto error = ::testing::internal::GetCapturedStderr();
    EXPECT_NE(error.find("Cannot open log file"), std::string::npos);
    EXPECT_NE(Logger::get_console_logger(), nullptr);
    EXPECT_TRUE(Logger::get_log_file_path().empty());
    const auto contents = read_text_file(blocked);
    ASSERT_TRUE(contents);
    EXPECT_EQ(contents.value(), "preserved");
}
