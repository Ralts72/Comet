#include "profiler.h"

namespace Comet {
    thread_local std::vector<Profiler::ActiveBlock> Profiler::s_thread_stack;
    std::mutex Profiler::s_mtx;
    std::unordered_map<std::string, ProfileRecord> Profiler::s_records;

    void Profiler::begin_sample(const char* label) {
        auto& stack = get_thread_stack();
        stack.push_back({label, std::chrono::high_resolution_clock::now()});
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
        std::lock_guard<std::mutex> lock(s_mtx);
        for(const auto& [label, record]: s_records) {
            const double avg = record.total_time / record.call_count;
            printf("[Profiler] %s: calls=%llu, total=%.3f ms, avg=%.3f ms\n",
                label.c_str(),
                static_cast<unsigned long long>(record.call_count),
                record.total_time,
                avg);
        }
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