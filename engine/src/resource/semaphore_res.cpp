#include "semaphore_res.h"
#include "../graphics/device.h"

namespace Comet {

    SemaphoreRes::SemaphoreRes(Device& device): m_device(device) {
        vk::SemaphoreCreateInfo ci{};
        m_semaphore = m_device.getVkDevice().createSemaphore(ci);
    }

    SemaphoreRes::~SemaphoreRes() {
        m_device.getVkDevice().destroySemaphore(m_semaphore);
    }

    void SemaphoreRes::decrease() {
        Refcount::decrease();
        if(getCount() == 0) {
            //TODO
            // m_device.m_pending_delete_semaphores.push_back(this);
        }
    }
}