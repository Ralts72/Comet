#pragma once
#include <cassert>
#include "singleton.h"
#include <spdlog/spdlog.h>

namespace Comet {
    class Log: public Singleton<Log, false> {
    public:
        Log();

        std::shared_ptr<spdlog::logger> m_console_logger;
        std::shared_ptr<spdlog::logger> m_file_logger;
    };

#define LOG_ERROR(fmt, ...)                                                \
    do {                                                                   \
        Log::getInstance().m_console_logger->error(fmt, ##__VA_ARGS__);    \
        Log::getInstance().m_file_logger->error(fmt, ##__VA_ARGS__);       \
    } while (0)
#define LOG_WARN(fmt, ...)                                                 \
    do {                                                                   \
        Log::getInstance().m_console_logger->warn(fmt, ##__VA_ARGS__);     \
        Log::getInstance().m_file_logger->warn(fmt, ##__VA_ARGS__);        \
    } while (0)
#define LOG_DEBUG(fmt, ...)                                                \
    do {                                                                   \
        Log::getInstance().m_console_logger->debug(fmt, ##__VA_ARGS__);    \
        Log::getInstance().m_file_logger->debug(fmt, ##__VA_ARGS__);       \
    } while (0)
#define LOG_INFO(fmt, ...)                                                 \
    do {                                                                   \
        Log::getInstance().m_console_logger->info(fmt, ##__VA_ARGS__);     \
        Log::getInstance().m_file_logger->info(fmt, ##__VA_ARGS__);        \
    } while (0)
#define LOG_TRACE(fmt, ...)                                                \
    do {                                                                   \
        Log::getInstance().m_console_logger->trace(fmt, ##__VA_ARGS__);    \
        Log::getInstance().m_file_logger->trace(fmt, ##__VA_ARGS__);       \
    } while (0)
#define LOG_FATAL(fmt, ...)                                                \
    do {                                                                   \
        Log::getInstance().m_console_logger->critical(fmt, ##__VA_ARGS__); \
        Log::getInstance().m_file_logger->critical(fmt, ##__VA_ARGS__);    \
        assert(false);                                                     \
    } while (0)
}
