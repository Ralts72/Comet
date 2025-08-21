#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;

    class Fence {
    public:
        explicit Fence(Device* device);
        ~Fence();

        Fence(const Fence&) = delete;
        Fence& operator=(const Fence&) = delete;

        Fence(Fence&& other) noexcept;
        Fence& operator=(Fence&& other) noexcept;

        [[nodiscard]] vk::Fence get() const {return m_fence;}
    private:
        Device* m_device;
        vk::Fence m_fence;
    };
}
