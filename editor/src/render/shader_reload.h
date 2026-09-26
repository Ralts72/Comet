#pragma once

#include "file_recheck_trigger.h"
#include "shader/compiler.h"
#include "common/retry_backoff.h"

#include <map>
#include <chrono>
#include <cstddef>
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
            std::size_t compiled_stages = 0;
            bool succeeded = false;
        };

        ShaderReload(Comet::TaskScheduler& scheduler, Requests requests,
            std::filesystem::path watch_root = {},
            std::chrono::milliseconds quiet_period = DEFAULT_FILE_CHANGE_QUIET_PERIOD);
        void request(Clock::time_point now = Clock::now());
        // 调用方决定是否重试消费；不重新编译，交付前仍复核输入与 revision。
        bool retry_delivery(uint64_t revision, Clock::time_point now = Clock::now());
        [[nodiscard]] std::shared_ptr<const Compilation> update(
            Clock::time_point now = Clock::now());
        [[nodiscard]] bool uses_native_notifications() const {
            return m_changes.uses_native_notifications();
        }

    private:
        struct Pending {
            std::shared_ptr<Compilation> output;
            std::future<void> completion;
        };
        static bool inputs_unchanged(const Compilation& compilation);
        static void compile_batch(Compilation& output, const Requests& requests,
            const std::shared_ptr<const Compilation>& previous);

        Comet::TaskScheduler& m_scheduler;
        Requests m_requests;
        FileRecheckTrigger m_changes;
        std::chrono::milliseconds m_quiet_period;
        std::optional<Pending> m_pending;
        std::shared_ptr<const Compilation> m_observed;
        Comet::RetryBackoff m_delivery_retry;
        uint64_t m_revision = 1;
        bool m_requested = true;
        Clock::time_point m_due{};
    };
}
