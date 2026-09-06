#pragma once

#include "core/task_scheduler.h"
#include "graphics/pipeline/shader.h"
#include "shader/compiler.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>

namespace CometEditor {
    // 一个编译/发布组；只产出 CPU 候选，不持有 Renderer/Device 或发布回调。
    class ShaderReload {
    public:
        using Clock = std::chrono::steady_clock;
        using Requests = std::map<std::string, Comet::ShaderCompiler::Request>;
        struct Options {
            std::chrono::milliseconds poll_interval{200};
            std::chrono::milliseconds debounce{150};
        };
        struct Statistics {
            uint64_t revision = 1;
            uint64_t submitted = 0;
            uint64_t discarded = 0;
            uint64_t failed = 0;
            uint64_t backpressure = 0;
        };

        ShaderReload(Comet::TaskScheduler& scheduler, Requests requests);
        ShaderReload(Comet::TaskScheduler& scheduler, Requests requests, Options options);
        [[nodiscard]] std::optional<Comet::ShaderManager::Bytecodes> update(
            Clock::time_point now = Clock::now());
        [[nodiscard]] bool is_busy() const {
            return m_pending || m_in_flight.has_value();
        }
        [[nodiscard]] const Statistics& get_statistics() const { return m_statistics; }

    private:
        struct Stamp {
            std::filesystem::file_time_type modified{};
            uintmax_t size = 0;
            int error = 0;
            bool operator==(const Stamp&) const = default;
        };
        struct Job {
            uint64_t revision;
            Comet::ShaderManager::Bytecodes bytecodes;
            std::vector<Comet::ShaderCompiler::Result> inputs;
            std::string error;
        };
        struct InFlight {
            std::shared_ptr<Job> job;
            std::future<void> completion;
        };
        [[nodiscard]] static Stamp read_stamp(const std::filesystem::path& path);
        void watch_inputs(const Job& job);
        void request_update(Clock::time_point now);

        Comet::TaskScheduler& m_scheduler;
        Requests m_requests;
        Options m_options;
        std::map<std::filesystem::path, Stamp> m_watched;
        std::optional<InFlight> m_in_flight;
        bool m_pending = true;
        Clock::time_point m_next_poll{};
        Clock::time_point m_debounce_until{};
        Statistics m_statistics;
    };
}
