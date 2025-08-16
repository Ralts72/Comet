#include "pch.h"
#include "log_system.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>

namespace Comet {
    std::shared_ptr<spdlog::logger> LogSystem::s_console_logger = nullptr;
    bool LogSystem::s_initialized = false;
    
    void LogSystem::init() {
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
    
    void LogSystem::shutdown() {
        if (s_console_logger) {
            s_console_logger.reset();
        }
        spdlog::shutdown();
        s_initialized = false;
    }
    
    std::shared_ptr<spdlog::logger> LogSystem::get_console_logger() {
        if (!s_initialized) {
            init(); // 延迟初始化
        }
        return s_console_logger;
    }
    
    void LogSystem::set_test_logger(std::shared_ptr<spdlog::logger> logger) {
        s_console_logger = logger;
        s_initialized = true;
    }
}
