#include "memory_res.h"

#include "graphics/device.h"

namespace Comet {
    MemoryRes::MemoryRes(Device& device, uint64_t size, uint32_t memory_type_index): m_device(device) {
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = size;
        allocInfo.memoryTypeIndex = memory_type_index;
        m_memory = m_device.getVkDevice().allocateMemory(allocInfo);

        if(m_memory) {
            m_size = size;
        }
    }

    MemoryRes::~MemoryRes() {
        m_device.getVkDevice().freeMemory(m_memory);
    }
}
