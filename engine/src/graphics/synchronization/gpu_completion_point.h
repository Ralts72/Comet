#pragma once

#include "common/export.h"

#include <cstdint>

namespace Comet {
    class Queue;
    class Semaphore;
    struct QueueSemaphoreSubmit;

    class COMET_API GpuCompletionPoint {
    public:
        GpuCompletionPoint() = default;

        [[nodiscard]] bool is_valid() const noexcept {
            return m_timeline != nullptr && m_value > 0;
        }

        [[nodiscard]] uint64_t get_value() const noexcept { return m_value; }
        [[nodiscard]] bool is_complete() const;
        void wait() const;
        [[nodiscard]] bool wait_for(uint64_t timeout) const;

        bool operator==(const GpuCompletionPoint&) const noexcept = default;

    private:
        friend class Queue;
        friend struct QueueSemaphoreSubmit;

        GpuCompletionPoint(
            const Semaphore& timeline,
            uint64_t value)
            : m_timeline(&timeline), m_value(value) {}

        const Semaphore* m_timeline = nullptr;
        uint64_t m_value = 0;
    };
}
