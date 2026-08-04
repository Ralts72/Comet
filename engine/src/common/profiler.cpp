#include "profiler.h"
#include "logger.h"

namespace Comet {
#ifdef BUILD_TYPE_DEBUG
    namespace {
        spdlog::level::level_enum select_profile_log_level(const int call_count, const double average) {
            if(call_count == 1) return spdlog::level::info;
            if(average > 50.0) return spdlog::level::critical;
            if(average > 10.0) return spdlog::level::err;
            if(average > 5.0) return spdlog::level::warn;
            if(average > 1.0) return spdlog::level::info;
            return spdlog::level::debug;
        }
    }
#endif

    thread_local std::vector<Profiler::ActiveBlock> Profiler::s_thread_stack;
    std::mutex Profiler::s_mtx;
    std::unordered_map<std::string, ProfileRecord> Profiler::s_records;

    void Profiler::begin_sample(const char* label) {
        auto& stack = get_thread_stack();
        stack.push_back({.label = label, .start = std::chrono::high_resolution_clock::now()});
    }

    void Profiler::end_sample() {
        auto& stack = get_thread_stack();
        if(stack.empty()) return;

        auto [label, start] = stack.back();
        stack.pop_back();

        const auto duration = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - start
        ).count();

        std::lock_guard<std::mutex> lock(s_mtx);
        auto& [total_time, call_count] = s_records[label];
        total_time += duration;
        call_count++;
    }

    void Profiler::dump_results() {
#ifdef BUILD_TYPE_DEBUG
        const auto logger = Logger::get_profiler_logger();
        if(!logger) return;

        decltype(s_records) records; {
            std::lock_guard<std::mutex> lock(s_mtx);
            records = s_records;
        }

        for(const auto& [label, record]: records) {
            const auto& [total_time, call_count] = record;
            if(call_count <= 0) continue;

            const double average = total_time / call_count;
            const auto level = select_profile_log_level(call_count, average);

            logger->log(
                level,
                "{:<30}  calls={:<8}  total={:>10.3f} ms    avg={:>10.3f} ms",
                label,
                call_count,
                total_time,
                average
            );
        }
#endif
    }

    void Profiler::reset() {
        std::lock_guard<std::mutex> lock(s_mtx);
        s_records.clear();
    }

    std::vector<Profiler::ActiveBlock>& Profiler::get_thread_stack() {
        return s_thread_stack;
    }

    ScopedSample::ScopedSample(const char* label) {
        Profiler::begin_sample(label);
    }

    ScopedSample::~ScopedSample() {
        Profiler::end_sample();
    }
}
