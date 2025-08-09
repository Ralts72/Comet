#include "pch.h"
#include "log_system.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>

namespace Comet {
    LogSystem::LogSystem() {
        m_console_logger = spdlog::stdout_color_mt<spdlog::async_factory>("console");
        m_console_logger->set_level(spdlog::level::trace);
    }
}
