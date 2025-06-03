#pragma once
#include "../pch.h"
#include "refcount.h"

namespace Comet {
    class Device;

    class SemaphoreRes : public Refcount
    {
    public:
        SemaphoreRes(Device&);
        SemaphoreRes(const SemaphoreRes&) = delete;
        SemaphoreRes(SemaphoreRes&&) = delete;
        SemaphoreRes& operator=(const SemaphoreRes&) = delete;
        SemaphoreRes& operator=(SemaphoreRes&&) = delete;

        ~SemaphoreRes();
        void decrease() override;

        [[nodiscard]] vk::Semaphore getVkSemaphore() const { return m_semaphore; }
    private:
        vk::Semaphore m_semaphore;
        Device &m_device;
    };
}