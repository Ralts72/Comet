#pragma once
#include "../pch.h"
#include "refcount.h"

namespace Comet {
    class Device;

    class SemaphoreRes final: public Refcount {
    public:
        explicit SemaphoreRes(Device&);

        SemaphoreRes(const SemaphoreRes&) = delete;

        SemaphoreRes(SemaphoreRes&&) = delete;

        SemaphoreRes& operator=(const SemaphoreRes&) = delete;

        SemaphoreRes& operator=(SemaphoreRes&&) = delete;

        ~SemaphoreRes() override;

        void decrease() override;

        [[nodiscard]] vk::Semaphore getVkSemaphore() const { return m_semaphore; }

    private:
        vk::Semaphore m_semaphore;
        Device& m_device;
    };
}
