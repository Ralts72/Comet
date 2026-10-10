#pragma once

#include "common/result.h"
#include "common/export.h"

#include <chrono>
#include <optional>

namespace Comet {
    // 每帧只等待剩余预算；GPU／VSync 已耗尽预算时不追加等待，也不追赶旧期限。
    class COMET_API FramePacer final {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr int MAX_LIMIT = 1000;

        [[nodiscard]] static Result<void> validate_limit(int limit);
        [[nodiscard]] Result<void> set_limit(int limit);
        [[nodiscard]] int limit() const { return m_limit; }
        [[nodiscard]] std::optional<Clock::time_point> deadline(
            Clock::time_point frame_start) const;

    private:
        int m_limit = 0;
        Clock::duration m_interval{};
    };
}
