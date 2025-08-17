#include "fence.h"
#include "device.h"

namespace Comet {
    Fence::Fence(Device* device): m_device(device) {
        vk::FenceCreateInfo fence_create_info = {};
        fence_create_info.flags = vk::FenceCreateFlagBits::eSignaled;
        m_fence = m_device->get_device().createFence(fence_create_info);
    }

    Fence::~Fence() {
        if(m_device && m_fence!= VK_NULL_HANDLE) {
            m_device->get_device().destroyFence(m_fence);
        }
    }

    Fence::Fence(Fence&& other) noexcept
        : m_device(other.m_device), m_fence(other.m_fence) {
        other.m_device = nullptr;
        other.m_fence = VK_NULL_HANDLE;
    }

    Fence& Fence::operator=(Fence&& other) noexcept {
        if (this != &other) {
            if (m_fence != VK_NULL_HANDLE && m_device) {
                m_device->get_device().destroyFence(m_fence);
            }
            m_device = other.m_device;
            m_fence = other.m_fence;
            other.m_device = nullptr;
            other.m_fence = VK_NULL_HANDLE;
        }
        return *this;
    }
}
