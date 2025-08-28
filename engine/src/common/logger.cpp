#include "pch.h"
#include "logger.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>

namespace Comet {
    std::shared_ptr<spdlog::logger> Logger::s_console_logger = nullptr;
    std::shared_ptr<spdlog::logger> Logger::s_profiler_logger = nullptr;
    bool Logger::s_initialized = false;
    std::string Logger::s_current_log_file_path;
    bool Logger::s_enable_file_output = false;

    void Logger::init() {
        if(s_initialized) {
            return;
        }

        // 直接使用绝对路径到logs目录
        std::filesystem::path logs_dir = std::string(PROJECT_ROOT_DIR) + "/logs";

        // 生成一次时间戳，两个logger共享
        static std::string shared_timestamp;
        if(shared_timestamp.empty()) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch()) % 1000;

            std::stringstream ss;
            ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
                    << "_" << std::setfill('0') << std::setw(3) << ms.count();
            shared_timestamp = ss.str();
        }

        // 生成日志文件名
        std::string log_filename = (logs_dir / ("comet_" + shared_timestamp + ".log")).string();
        std::string profiler_filename = (logs_dir / ("profiler_" + shared_timestamp + ".log")).string();
        s_current_log_file_path = log_filename;

        // 创建共享的控制台sink
        auto shared_console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        // 创建console logger (控制台 + 文件)
        s_console_logger = spdlog::get("console");
        if(!s_console_logger) {
            // 设置格式
            shared_console_sink->set_pattern("%^[%T] [%l] %v%$");

            if(s_enable_file_output) {
                auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_filename, false);
                file_sink->set_pattern("[%Y-%m-%d %T.%e] [%l] %v");

                // 创建组合logger (控制台 + 文件)
                s_console_logger = std::make_shared<spdlog::logger>("console",
                    spdlog::sinks_init_list{shared_console_sink, file_sink});
            } else {
                // 仅控制台输出
                s_console_logger = std::make_shared<spdlog::logger>("console", shared_console_sink);
            }

            s_console_logger->set_level(spdlog::level::trace);
            s_console_logger->flush_on(spdlog::level::trace);

            // 注册logger
            spdlog::register_logger(s_console_logger);
        }

        // 创建profiler logger
        s_profiler_logger = spdlog::get("profiler");
        if(!s_profiler_logger) {
            auto profiler_console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            profiler_console_sink->set_pattern("%^[Profiler] %-50v%$");

            if(s_enable_file_output) {
                auto profiler_file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(profiler_filename, false);
                profiler_file_sink->set_pattern("[%Y-%m-%d %T.%e] [Profiler] %v");

                // 创建组合logger (控制台 + 文件)
                s_profiler_logger = std::make_shared<spdlog::logger>("profiler",
                    spdlog::sinks_init_list{profiler_console_sink, profiler_file_sink});
            } else {
                // 仅控制台输出
                s_profiler_logger = std::make_shared<spdlog::logger>("profiler", profiler_console_sink);
            }

            s_profiler_logger->set_level(spdlog::level::trace);
            s_profiler_logger->flush_on(spdlog::level::trace);

            // 注册logger
            spdlog::register_logger(s_profiler_logger);
        }

        // 设置全局刷新策略
        spdlog::flush_every(std::chrono::seconds(1)); // 每秒自动刷新

        s_initialized = true;
    }

    void Logger::shutdown() {
        if(s_console_logger) {
            s_console_logger->flush(); // 强制刷新缓冲区
            s_console_logger.reset();
        }
        if(s_profiler_logger) {
            s_profiler_logger->flush(); // 强制刷新缓冲区
            s_profiler_logger.reset();
        }
        spdlog::shutdown();
        s_initialized = false;
        s_current_log_file_path.clear();
    }

    std::shared_ptr<spdlog::logger> Logger::get_console_logger() {
        if(!s_initialized) {
            init(); // 延迟初始化
        }
        return s_console_logger;
    }

    std::shared_ptr<spdlog::logger> Logger::get_profiler_logger() {
        if(!s_initialized) {
            init(); // 延迟初始化
        }
        return s_profiler_logger;
    }

    std::string Logger::get_log_file_path() {
        if(!s_initialized) {
            init(); // 延迟初始化
        }
        return s_current_log_file_path;
    }
}
