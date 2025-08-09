#pragma once
#include <cassert>
#include "../singleton.h"
#include "../export.h"
#include <spdlog/spdlog.h>

namespace Comet {
    class COMET_API LogSystem: public Singleton<LogSystem, false> {
    public:
        LogSystem();

        std::shared_ptr<spdlog::logger> m_console_logger;
    };

#define LOG_ERROR(fmt, ...)                                                            \
    do {                                                                               \
        ::Comet::LogSystem::instance().m_console_logger->error(fmt, ##__VA_ARGS__);    \
    } while (0)
#define LOG_WARN(fmt, ...)                                                             \
    do {                                                                               \
        ::Comet::LogSystem::instance().m_console_logger->warn(fmt, ##__VA_ARGS__);     \
    } while (0)
#define LOG_DEBUG(fmt, ...)                                                            \
    do {                                                                               \
        ::Comet::LogSystem::instance().m_console_logger->debug(fmt, ##__VA_ARGS__);    \
    } while (0)
#define LOG_INFO(fmt, ...)                                                             \
    do {                                                                               \
        ::Comet::LogSystem::instance().m_console_logger->info(fmt, ##__VA_ARGS__);     \
    } while (0)
#define LOG_TRACE(fmt, ...)                                                            \
    do {                                                                               \
        ::Comet::LogSystem::instance().m_console_logger->trace(fmt, ##__VA_ARGS__);    \
    } while(0)
#define LOG_FATAL(fmt, ...)                                                            \
    do {                                                                               \
        ::Comet::LogSystem::instance().m_console_logger->trace(fmt, ##__VA_ARGS__);    \
        assert(false);                                                                 \
    } while(0)
}
