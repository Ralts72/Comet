#include "pch.h"
#include "log.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "macro.h"

namespace Comet {
    Log::Log() {
        SDL_Time time;
        SDL_CHECK(SDL_GetCurrentTime(&time));
        SDL_DateTime dt;
        SDL_CHECK(SDL_TimeToDateTime(time, &dt, true));

        const std::string log_filename = fmt::format("log/{}-{}-{}_{}-{}-{}.log", dt.year, dt.month, dt.day,
            dt.hour, dt.minute, dt.second);
        m_console_logger = spdlog::stdout_color_mt("console");
        m_console_logger->set_level(spdlog::level::trace);
        m_file_logger = spdlog::basic_logger_mt("file_logger", log_filename);
        m_file_logger->set_level(spdlog::level::trace);
    }
}
