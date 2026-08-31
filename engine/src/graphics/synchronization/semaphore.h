#pragma once
#include "graphics/vk_common.h"

#include <cstdint>

namespace Comet {
    class Device;

    class Semaphore {
    public:
        enum class Type {
            Binary,
            Timeline
        };

        explicit Semaphore(
            Device& device,
            Type type = Type::Binary,
            uint64_t initial_value = 0);
        ~Semaphore();

        Semaphore(const Semaphore&) = delete;
        Semaphore& operator=(const Semaphore&) = delete;

        Semaphore(Semaphore&& other) noexcept;
        Semaphore& operator=(Semaphore&& other) noexcept;

        [[nodiscard]] vk::Semaphore get() const { return m_semaphore; }
        [[nodiscard]] Type get_type() const { return m_type; }
        [[nodiscard]] uint64_t get_counter_value() const;
        void wait(uint64_t value) const;
        [[nodiscard]] bool wait_for(uint64_t value, uint64_t timeout) const;

    private:
        Device* m_device = nullptr;
        vk::Semaphore m_semaphore{};
        Type m_type = Type::Binary;
    };
}
