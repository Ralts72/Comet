#pragma once
#include "common/export.h"
#include <spdlog/spdlog.h>

namespace Comet {
    // 统一的日志级别枚举
    enum class COMET_API LogLevel {
        Trace,
        Debug,
        Info,
        Warning,
        Error,
        Critical
    };

    COMET_API LogLevel log_level_from_spdlog(spdlog::level::level_enum level);

    COMET_API spdlog::level::level_enum log_level_to_spdlog(LogLevel level);

    COMET_API const char* log_level_to_string(LogLevel level);

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

        // 获取日志文件路径
        static std::string get_log_file_path();

        // 移除控制台输出 sink（stdout/console sinks）
        // 用于在编辑器等场景中禁用控制台输出，只保留文件输出
        static void remove_console_sinks();

        // 添加自定义 sink 到 console logger
        // 允许外部（如编辑器）注册自定义的日志处理逻辑
        static void add_custom_sink(std::shared_ptr<spdlog::sinks::sink> sink);

        // 获取 logs 目录路径
        static std::string get_logs_directory();

        // 生成带时间戳的文件名（不含扩展名）
        // 格式：YYYYMMDD_HHMMSS_MMM
        static std::string generate_timestamp_filename();

    private:
        static std::shared_ptr<spdlog::logger> s_console_logger;
        static std::shared_ptr<spdlog::logger> s_profiler_logger;
        static bool s_initialized;
        static std::string s_current_log_file_path;
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