#pragma once
#include "common/export.h"
#include <spdlog/spdlog.h>

namespace Comet {
    class COMET_API Logger {
    public:
        // 禁止实例化
        Logger() = delete;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        
        // 静态初始化和清理
        static void init();
        static void shutdown();
        
        // 获取logger
        static std::shared_ptr<spdlog::logger> get_console_logger();
        static std::shared_ptr<spdlog::logger> get_profiler_logger();
        
        // 测试专用接口
        static void set_test_logger(std::shared_ptr<spdlog::logger> logger);
        
    private:
        static std::shared_ptr<spdlog::logger> s_console_logger;
        static std::shared_ptr<spdlog::logger> s_profiler_logger;
        static bool s_initialized;
    };

#ifdef BUILD_TYPE_DEBUG
#define LOG_ERROR(fmt, ...)                                                            \
    do {                                                                               \
        if (auto logger = ::Comet::Logger::get_console_logger()) {                     \
            logger->error(fmt, ##__VA_ARGS__);                                         \
        }                                                                              \
    } while (0)
#define LOG_WARN(fmt, ...)                                                             \
    do {                                                                               \
        if (auto logger = ::Comet::Logger::get_console_logger()) {                     \
            logger->warn(fmt, ##__VA_ARGS__);                                          \
        }                                                                              \
    } while (0)
#define LOG_DEBUG(fmt, ...)                                                            \
    do {                                                                               \
        if (auto logger = ::Comet::Logger::get_console_logger()) {                     \
            logger->debug(fmt, ##__VA_ARGS__);                                         \
        }                                                                              \
    } while (0)
#define LOG_INFO(fmt, ...)                                                             \
    do {                                                                               \
        if (auto logger = ::Comet::Logger::get_console_logger()) {                     \
            logger->info(fmt, ##__VA_ARGS__);                                          \
        }                                                                              \
    } while (0)
#define LOG_TRACE(fmt, ...)                                                            \
    do {                                                                               \
        if (auto logger = ::Comet::Logger::get_console_logger()) {                     \
            logger->trace(fmt, ##__VA_ARGS__);                                         \
        }                                                                              \
    } while(0)
#else
// Release模式下除LOG_FATAL外的LOG都为空操作
#define LOG_ERROR(fmt, ...)   do { } while (0)
#define LOG_WARN(fmt, ...)    do { } while (0)  
#define LOG_DEBUG(fmt, ...)   do { } while (0)
#define LOG_INFO(fmt, ...)    do { } while (0)
#define LOG_TRACE(fmt, ...)   do { } while (0)
#endif
#define LOG_FATAL(fmt, ...)                                                            \
    do {                                                                               \
        if (auto logger = ::Comet::Logger::get_console_logger()) {                     \
            logger->critical(fmt, ##__VA_ARGS__);                                      \
        }                                                                              \
        assert(false);                                                                 \
        std::terminate();                                                              \
    } while(0)
}


/// Profiler专用日志宏，格式对齐的 [Profiler] 内容
#ifdef BUILD_TYPE_DEBUG
#define PROFILE_LOG_CRITICAL(fmt, ...)                                                 \
    do {                                                                               \
        if (auto logger = ::Comet::Logger::get_profiler_logger()) {                    \
            logger->critical(fmt, ##__VA_ARGS__);                                      \
        }                                                                              \
    } while (0)

#define PROFILE_LOG_HIGH(fmt, ...)                                                     \
    do {                                                                               \
        if (auto logger = ::Comet::Logger::get_profiler_logger()) {                    \
            logger->error(fmt, ##__VA_ARGS__);                                         \
        }                                                                              \
    } while (0)

#define PROFILE_LOG_MEDIUM(fmt, ...)                                                   \
    do {                                                                               \
        if (auto logger = ::Comet::Logger::get_profiler_logger()) {                    \
            logger->warn(fmt, ##__VA_ARGS__);                                          \
        }                                                                              \
    } while (0)

#define PROFILE_LOG_LOW(fmt, ...)                                                      \
    do {                                                                               \
        if (auto logger = ::Comet::Logger::get_profiler_logger()) {                    \
            logger->info(fmt, ##__VA_ARGS__);                                          \
        }                                                                              \
    } while (0)

#define PROFILE_LOG_TRACE(fmt, ...)                                                    \
    do {                                                                               \
        if (auto logger = ::Comet::Logger::get_profiler_logger()) {                    \
            logger->debug(fmt, ##__VA_ARGS__);                                         \
        }                                                                              \
    } while (0)
#else
// Release模式下profiler日志都为空操作
#define PROFILE_LOG_CRITICAL(fmt, ...)  do { } while (0)
#define PROFILE_LOG_HIGH(fmt, ...)      do { } while (0)
#define PROFILE_LOG_MEDIUM(fmt, ...)    do { } while (0)  
#define PROFILE_LOG_LOW(fmt, ...)       do { } while (0)
#define PROFILE_LOG_TRACE(fmt, ...)     do { } while (0)
#endif