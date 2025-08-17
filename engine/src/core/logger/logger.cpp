#include "pch.h"
#include "logger.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>

namespace Comet {
    std::shared_ptr<spdlog::logger> Logger::s_console_logger = nullptr;
    bool Logger::s_initialized = false;
    
    void Logger::init() {
        if (s_initialized) {
            return;
        }
        
        s_console_logger = spdlog::get("console");
        if (!s_console_logger) {
            s_console_logger = spdlog::stdout_color_mt<spdlog::async_factory>("console");
            s_console_logger->set_level(spdlog::level::trace);
        }
        
        s_initialized = true;
    }
    
    void Logger::shutdown() {
        if (s_console_logger) {
            s_console_logger.reset();
        }
        spdlog::shutdown();
        s_initialized = false;
    }
    
    std::shared_ptr<spdlog::logger> Logger::get_console_logger() {
        if (!s_initialized) {
            init(); // 延迟初始化
        }
        return s_console_logger;
    }
    
    void Logger::set_test_logger(std::shared_ptr<spdlog::logger> logger) {
        s_console_logger = logger;
        s_initialized = true;
    }
}
