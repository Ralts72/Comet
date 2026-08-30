#pragma once
#include "graphics/vk_common.h"

#include <cstdint>
#include <limits>

namespace Comet {
    class Device;

    enum class SemaphoreType {
        Binary,
        Timeline
    };

    class Semaphore {
    public:
        explicit Semaphore(
            Device& device,
            SemaphoreType type = SemaphoreType::Binary,
            uint64_t initial_value = 0);
        ~Semaphore();

        Semaphore(const Semaphore&) = delete;
        Semaphore& operator=(const Semaphore&) = delete;

        Semaphore(Semaphore&& other) noexcept;
        Semaphore& operator=(Semaphore&& other) noexcept;

        [[nodiscard]] vk::Semaphore get() const { return m_semaphore; }
        [[nodiscard]] SemaphoreType get_type() const { return m_type; }
        [[nodiscard]] uint64_t get_counter_value() const;
        [[nodiscard]] bool wait(
            uint64_t value,
            uint64_t timeout = std::numeric_limits<uint64_t>::max()) const;

    private:
        Device* m_device = nullptr;
        vk::Semaphore m_semaphore{};
        SemaphoreType m_type = SemaphoreType::Binary;
    };
}
