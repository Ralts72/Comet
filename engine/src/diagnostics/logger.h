#pragma once
#include "common/export.h"
#include "config/config.h"

#include <cassert>
#include <exception>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace Comet {
    enum class COMET_API LogLevel { Trace, Debug, Info, Warning, Error, Critical };

    COMET_API LogLevel log_level_from_spdlog(spdlog::level::level_enum level);
    class COMET_API Logger {
    public:
        Logger() = delete;

        Logger(const Logger&) = delete;

        Logger& operator=(const Logger&) = delete;

        static void init(const Config::Log& config = {}, bool enable_profiler = false);

        static void shutdown();

        static std::shared_ptr<spdlog::logger> get_console_logger();

        static std::shared_ptr<spdlog::logger> get_profiler_logger();

        static std::string get_log_file_path();

        // 仅移除标准输出端，保留文件及自定义输出端。
        static void remove_console_sinks();

        // 向应用日志添加输出端，不影响性能采样日志。
        static void add_custom_sink(const std::shared_ptr<spdlog::sinks::sink>& sink);

    private:
        static std::shared_ptr<spdlog::logger> s_console_logger;
        static std::shared_ptr<spdlog::logger> s_profiler_logger;
        static bool s_initialized;
        static std::string s_current_log_file_path;
    };

#define LOG_ERROR(fmt, ...)                                                              \
    do {                                                                                 \
        if(auto logger = ::Comet::Logger::get_console_logger();                          \
            logger && logger->should_log(::spdlog::level::err)) {                        \
            logger->error(fmt __VA_OPT__(, ) __VA_ARGS__);                               \
        }                                                                                \
    } while(0)
#define LOG_WARN(fmt, ...)                                                               \
    do {                                                                                 \
        if(auto logger = ::Comet::Logger::get_console_logger();                          \
            logger && logger->should_log(::spdlog::level::warn)) {                       \
            logger->warn(fmt __VA_OPT__(, ) __VA_ARGS__);                                \
        }                                                                                \
    } while(0)
#define LOG_INFO(fmt, ...)                                                               \
    do {                                                                                 \
        if(auto logger = ::Comet::Logger::get_console_logger();                          \
            logger && logger->should_log(::spdlog::level::info)) {                       \
            logger->info(fmt __VA_OPT__(, ) __VA_ARGS__);                                \
        }                                                                                \
    } while(0)

#ifdef COMET_ENABLE_DEBUG_LOGS
#define LOG_DEBUG(fmt, ...)                                                              \
    do {                                                                                 \
        if(auto logger = ::Comet::Logger::get_console_logger();                          \
            logger && logger->should_log(::spdlog::level::debug)) {                      \
            logger->debug(fmt __VA_OPT__(, ) __VA_ARGS__);                               \
        }                                                                                \
    } while(0)
#else
#define LOG_DEBUG(fmt, ...)                                                              \
    do {                                                                                 \
    } while(0)
#endif

#ifdef COMET_ENABLE_TRACE_LOGS
#define LOG_TRACE(fmt, ...)                                                              \
    do {                                                                                 \
        if(auto logger = ::Comet::Logger::get_console_logger();                          \
            logger && logger->should_log(::spdlog::level::trace)) {                      \
            logger->trace(fmt __VA_OPT__(, ) __VA_ARGS__);                               \
        }                                                                                \
    } while(0)
#else
#define LOG_TRACE(fmt, ...)                                                              \
    do {                                                                                 \
    } while(0)
#endif

#define LOG_FATAL(fmt, ...)                                                              \
    do {                                                                                 \
        if(auto logger = ::Comet::Logger::get_console_logger()) {                        \
            logger->critical(fmt __VA_OPT__(, ) __VA_ARGS__);                            \
            logger->flush();                                                             \
        }                                                                                \
        assert(false);                                                                   \
        std::terminate();                                                                \
    } while(0)
}
