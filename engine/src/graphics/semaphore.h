#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;

    class Semaphore {
    public:
        explicit Semaphore(Device* device);
        ~Semaphore();

        Semaphore(const Semaphore&) = delete;
        Semaphore& operator=(const Semaphore&) = delete;

        Semaphore(Semaphore&& other) noexcept;
        Semaphore& operator=(Semaphore&& other) noexcept;

        [[nodiscard]] vk::Semaphore get_semaphore() const { return m_semaphore; }
    private:
        Device* m_device;
        vk::Semaphore m_semaphore;
    };
}
