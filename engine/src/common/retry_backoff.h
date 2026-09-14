#pragma once

#include "common/export.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace Comet {
    // 只记录重试时序；首次执行、错误分类与请求身份由调用方负责。
    class COMET_API RetryBackoff {
    public:
        using Clock = std::chrono::steady_clock;
        struct Policy {
            std::chrono::milliseconds initial_delay{1000};
            std::chrono::milliseconds max_delay{4000};
            uint32_t max_retries = 3;
        };

        RetryBackoff() = default;
        // 策略要求 0 < initial_delay <= max_delay；max_retries 为零可禁用重试。
        explicit RetryBackoff(Policy policy);

        // 重复预约不延后期限、不扣额度；consume 成功后不会自动预约下一次。
        [[nodiscard]] bool schedule(Clock::time_point now);
        [[nodiscard]] bool consume(Clock::time_point now);
        void reset();
        [[nodiscard]] uint32_t retry_count() const { return m_retry_count; }

    private:
        Policy m_policy;
        std::optional<Clock::time_point> m_due;
        uint32_t m_retry_count = 0;
    };
}
