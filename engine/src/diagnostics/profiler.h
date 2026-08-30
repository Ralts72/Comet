#pragma once

#include "common/export.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Comet {
    struct ProfileRecord {
        double total_time = 0.0;
        int call_count = 0;
    };

    class COMET_API Profiler {
    public:
        static void set_enabled(bool enabled) noexcept;

        [[nodiscard]] static bool is_enabled() noexcept;

        [[nodiscard]] static bool is_available() noexcept;

        [[nodiscard]] static bool begin_sample(const char* label);

        static void end_sample();

        static void dump_results();

        static void reset();

    private:
        using TimePoint = std::chrono::high_resolution_clock::time_point;

        struct ActiveBlock {
            const char* label{};
            TimePoint start;
        };

        static thread_local std::vector<ActiveBlock> s_thread_stack;
        static std::atomic_bool s_enabled;
        static std::mutex s_mtx;
        static std::unordered_map<std::string, ProfileRecord> s_records;

        static std::vector<ActiveBlock>& get_thread_stack();
    };

    class COMET_API ScopedSample {
    public:
        explicit ScopedSample(const char* label);

        ~ScopedSample();

    private:
        bool m_active = false;
    };
}

#ifdef COMET_ENABLE_PROFILER
#define PROFILE_SCOPE(name) Comet::ScopedSample __scope_##__LINE__(name)
#define PROFILE_RESULTS() Comet::Profiler::dump_results()
#else
#define PROFILE_SCOPE(name) ((void)0)
#define PROFILE_RESULTS() ((void)0)
#endif
