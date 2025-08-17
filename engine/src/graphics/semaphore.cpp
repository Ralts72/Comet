#include "semaphore.h"
#include "device.h"

namespace Comet {
    Semaphore::Semaphore(Device* device): m_device(device) {
        vk::SemaphoreCreateInfo semaphore_create_info = {};
        m_semaphore = m_device->get_device().createSemaphore(semaphore_create_info);
    }

    Semaphore::~Semaphore() {
        if (m_semaphore != VK_NULL_HANDLE && m_device) {
            m_device->get_device().destroySemaphore(m_semaphore);
        }
    }

    Semaphore::Semaphore(Semaphore&& other) noexcept
        : m_device(other.m_device), m_semaphore(other.m_semaphore) {
        other.m_device = nullptr;
        other.m_semaphore = VK_NULL_HANDLE;
    }

    Semaphore& Semaphore::operator=(Semaphore&& other) noexcept {
        if (this != &other) {
            if (m_semaphore != VK_NULL_HANDLE && m_device) {
                m_device->get_device().destroySemaphore(m_semaphore);
            }
            m_device = other.m_device;
            m_semaphore = other.m_semaphore;
            other.m_device = nullptr;
            other.m_semaphore = VK_NULL_HANDLE;
        }
        return *this;
    }
}
