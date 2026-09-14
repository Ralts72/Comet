#pragma once

#include "shader/compiler.h"

#include <map>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>

namespace Comet {
    class TaskScheduler;
}

namespace CometEditor {
    // 主线程管理请求和消费；Worker 只持有输入副本及 CPU 结果。
    class ShaderReload final {
    public:
        using Clock = std::chrono::steady_clock;
        using Requests = std::map<std::string, Comet::ShaderCompiler::Request>;
        struct Compilation {
            uint64_t revision = 0;
            std::map<std::string, Comet::ShaderCompiler::Result> stages;
            std::string diagnostics;
            bool succeeded = false;
        };

        ShaderReload(Comet::TaskScheduler& scheduler, Requests requests);
        void request(Clock::time_point now = Clock::now());
        [[nodiscard]] std::shared_ptr<const Compilation> update(
            Clock::time_point now = Clock::now());

    private:
        struct Pending {
            std::shared_ptr<Compilation> output;
            std::future<void> completion;
        };
        static bool inputs_unchanged(const Compilation& compilation);

        Comet::TaskScheduler& m_scheduler;
        Requests m_requests;
        std::optional<Pending> m_pending;
        std::shared_ptr<const Compilation> m_observed;
        uint64_t m_revision = 1;
        bool m_requested = true;
        Clock::time_point m_due{};
        Clock::time_point m_next_poll{};
    };
}
