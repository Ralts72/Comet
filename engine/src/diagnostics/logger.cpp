#include "diagnostics/logger.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

namespace Comet {
    std::shared_ptr<spdlog::logger> Logger::s_console_logger = nullptr;
    std::shared_ptr<spdlog::logger> Logger::s_profiler_logger = nullptr;
    bool Logger::s_initialized = false;
    std::string Logger::s_current_log_file_path;

    static spdlog::level::level_enum parse_log_level(const std::string& level_str) {
        if(level_str == "trace")
            return spdlog::level::trace;
        if(level_str == "debug")
            return spdlog::level::debug;
        if(level_str == "info")
            return spdlog::level::info;
        if(level_str == "warn")
            return spdlog::level::warn;
        if(level_str == "error")
            return spdlog::level::err;
        if(level_str == "critical")
            return spdlog::level::critical;
        if(level_str == "off")
            return spdlog::level::off;
        return spdlog::level::info; // 默认级别
    }

    void Logger::init(const Config::Log& config, const bool enable_profiler) {
        if(s_initialized) {
            return;
        }

        // 直接使用绝对路径到logs目录
        std::filesystem::path logs_dir(std::string(PROJECT_ROOT_DIR));
        logs_dir /= "logs";

        if(config.enable_file_logging) {
            // 确保logs目录存在
            if(!std::filesystem::exists(logs_dir)) {
                std::filesystem::create_directories(logs_dir);
            }
        }

        // 生成一次时间戳，两个 logger 共享
        static std::string shared_timestamp;
        if(shared_timestamp.empty()) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch())
                      % 1000;

            std::stringstream ss;
            ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << "_"
               << std::setfill('0') << std::setw(3) << ms.count();
            shared_timestamp = ss.str();
        }

        // 生成日志文件名
        std::string log_filename;
#ifdef COMET_ENABLE_PROFILER
        std::string profiler_filename;
#endif
        if(config.enable_file_logging) {
            log_filename = (logs_dir / ("comet_" + shared_timestamp + ".log")).string();
#ifdef COMET_ENABLE_PROFILER
            if(enable_profiler) {
                profiler_filename =
                    (logs_dir / ("profiler_" + shared_timestamp + ".log")).string();
            }
#endif
            s_current_log_file_path = log_filename;
        }

        // 创建共享的 console sink
        auto shared_console_sink =
            std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        // 创建 console logger（控制台 + 文件）
        s_console_logger = spdlog::get("console");
        if(!s_console_logger) {
            // 设置格式
            shared_console_sink->set_pattern("%^[%T] [%l] %v%$");

            if(config.enable_file_logging) {
                auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                    log_filename, false);
                file_sink->set_pattern("[%Y-%m-%d %T.%e] [%l] %v");

                // 创建组合 logger（控制台 + 文件）
                s_console_logger = std::make_shared<spdlog::logger>(
                    "console", spdlog::sinks_init_list{shared_console_sink, file_sink});
            } else {
                // 仅控制台输出
                s_console_logger =
                    std::make_shared<spdlog::logger>("console", shared_console_sink);
            }

            spdlog::level::level_enum log_level = parse_log_level(config.level);

            s_console_logger->set_level(log_level);
            s_console_logger->flush_on(spdlog::level::err);

            // 注册 logger
            spdlog::register_logger(s_console_logger);
        }

#ifdef COMET_ENABLE_PROFILER
        if(enable_profiler) {
            // 创建 profiler logger
            s_profiler_logger = spdlog::get("profiler");
        }
        if(enable_profiler && !s_profiler_logger) {
            auto profiler_console_sink =
                std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            profiler_console_sink->set_pattern("%^[Profiler] %-50v%$");

            if(config.enable_file_logging) {
                auto profiler_file_sink =
                    std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                        profiler_filename, false);
                profiler_file_sink->set_pattern("[%Y-%m-%d %T.%e] [Profiler] %v");

                // 创建组合 logger（控制台 + 文件）
                s_profiler_logger = std::make_shared<spdlog::logger>("profiler",
                    spdlog::sinks_init_list{profiler_console_sink, profiler_file_sink});
            } else {
                // 仅控制台输出
                s_profiler_logger =
                    std::make_shared<spdlog::logger>("profiler", profiler_console_sink);
            }

            // profiler logger 始终使用 trace 级别
            s_profiler_logger->set_level(spdlog::level::trace);
            s_profiler_logger->flush_on(spdlog::level::err);

            // 注册 logger
            spdlog::register_logger(s_profiler_logger);
        }
#endif

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
        return s_console_logger;
    }

    std::shared_ptr<spdlog::logger> Logger::get_profiler_logger() {
        return s_profiler_logger;
    }

    std::string Logger::get_log_file_path() {
        return s_current_log_file_path;
    }

    void Logger::remove_console_sinks() {
        auto logger = get_console_logger();
        if(!logger) {
            return;
        }

        // 移除控制台输出（stdout_color_sink），只保留文件输出
        auto& sinks = logger->sinks();
        std::erase_if(sinks, [](const std::shared_ptr<spdlog::sinks::sink>& sink) {
            // 检查是否是 stdout_color_sink 或 stdout_sink
            return dynamic_cast<spdlog::sinks::stdout_color_sink_mt*>(sink.get())
                       != nullptr
                   || dynamic_cast<spdlog::sinks::stdout_color_sink_st*>(sink.get())
                          != nullptr
                   || dynamic_cast<spdlog::sinks::stdout_sink_mt*>(sink.get()) != nullptr
                   || dynamic_cast<spdlog::sinks::stdout_sink_st*>(sink.get()) != nullptr;
        });
    }

    void Logger::add_custom_sink(const std::shared_ptr<spdlog::sinks::sink>& sink) {
        const auto logger = get_console_logger();
        if(!logger || !sink) {
            return;
        }

        logger->sinks().push_back(sink);
    }

    LogLevel log_level_from_spdlog(const spdlog::level::level_enum level) {
        switch(level) {
            case spdlog::level::trace:
                return LogLevel::Trace;
            case spdlog::level::debug:
                return LogLevel::Debug;
            case spdlog::level::info:
                return LogLevel::Info;
            case spdlog::level::warn:
                return LogLevel::Warning;
            case spdlog::level::err:
                return LogLevel::Error;
            case spdlog::level::critical:
                return LogLevel::Critical;
            default:
                return LogLevel::Info;
        }
    }

}
