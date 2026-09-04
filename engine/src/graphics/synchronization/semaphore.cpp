#include "graphics/synchronization/semaphore.h"
#include "diagnostics/logger.h"
#include "graphics/device.h"

#include <limits>

namespace Comet {
    Semaphore::Semaphore(Device& device, const Type type, const uint64_t initial_value)
        : m_device(&device), m_type(type) {
        if(type == Type::Binary && initial_value != 0) {
            LOG_FATAL("Binary semaphore initial value must be zero");
        }

        vk::SemaphoreCreateInfo semaphore_create_info = {};
        vk::SemaphoreTypeCreateInfo type_create_info{};
        if(type == Type::Timeline) {
            type_create_info.semaphoreType = vk::SemaphoreType::eTimeline;
            type_create_info.initialValue = initial_value;
            semaphore_create_info.pNext = &type_create_info;
        }
        m_semaphore = m_device->get().createSemaphore(semaphore_create_info);
    }

    Semaphore::~Semaphore() {
        if(m_semaphore != VK_NULL_HANDLE && m_device) {
            m_device->get().destroySemaphore(m_semaphore);
        }
    }

    Semaphore::Semaphore(Semaphore&& other) noexcept
        : m_device(other.m_device), m_semaphore(other.m_semaphore), m_type(other.m_type) {
        other.m_device = nullptr;
        other.m_semaphore = VK_NULL_HANDLE;
        other.m_type = Type::Binary;
    }

    Semaphore& Semaphore::operator=(Semaphore&& other) noexcept {
        if(this != &other) {
            if(m_semaphore != VK_NULL_HANDLE && m_device) {
                m_device->get().destroySemaphore(m_semaphore);
            }
            m_device = other.m_device;
            m_semaphore = other.m_semaphore;
            m_type = other.m_type;
            other.m_device = nullptr;
            other.m_semaphore = VK_NULL_HANDLE;
            other.m_type = Type::Binary;
        }
        return *this;
    }

    uint64_t Semaphore::get_counter_value() const {
        if(m_type != Type::Timeline || !m_device || !m_semaphore) {
            LOG_FATAL(
                "Semaphore counter is only available for a valid timeline semaphore");
        }
        return m_device->get().getSemaphoreCounterValue(m_semaphore);
    }

    void Semaphore::wait(const uint64_t value) const {
        if(!wait_for(value, std::numeric_limits<uint64_t>::max())) {
            LOG_FATAL("Timeline semaphore wait timed out");
        }
    }

    bool Semaphore::wait_for(const uint64_t value, const uint64_t timeout) const {
        if(m_type != Type::Timeline || !m_device || !m_semaphore) {
            LOG_FATAL(
                "Semaphore wait value is only available for a valid timeline semaphore");
        }

        vk::SemaphoreWaitInfo wait_info{};
        wait_info.semaphoreCount = 1;
        wait_info.pSemaphores = &m_semaphore;
        wait_info.pValues = &value;
        const vk::Result result = m_device->get().waitSemaphores(wait_info, timeout);
        if(result == vk::Result::eTimeout) {
            return false;
        }
        if(result != vk::Result::eSuccess) {
            LOG_FATAL("Timeline semaphore wait failed: {}", vk::to_string(result));
        }
        return true;
    }
}
