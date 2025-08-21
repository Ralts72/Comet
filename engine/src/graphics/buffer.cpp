#include "buffer.h"
#include "command_buffer.h"
#include "device.h"
#include "queue.h"
#include "common/profiler.h"

namespace Comet {
    Buffer::Buffer(Device* device, const vk::BufferUsageFlags usage, const size_t size, void* data,
        const BufferMemoryType buffer_type)
    : m_device(device), m_size(size), m_buffer_type(buffer_type) {
        PROFILE_SCOPE("Buffer::Constructor");
        if(m_buffer_type == BufferMemoryType::HostVisible) {
            std::tie(m_buffer, m_memory) = create_buffer(vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, usage);
            write(data);
        } else {
            auto [stage_buffer, stage_memory] = create_buffer(vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                                 vk::BufferUsageFlagBits::eTransferSrc);
            void* mapping = m_device->get().mapMemory(stage_memory, 0, vk::WholeSize);
            memcpy(mapping, data, m_size);
            m_device->get().unmapMemory(stage_memory);
            std::tie(m_buffer, m_memory) = create_buffer(vk::MemoryPropertyFlagBits::eDeviceLocal, usage | vk::BufferUsageFlagBits::eTransferDst);

            m_device->copy_buffer(stage_buffer, m_buffer, m_size);

            m_device->get().destroyBuffer(stage_buffer);
            m_device->get().freeMemory(stage_memory);
        }
    }

    std::pair<vk::Buffer, vk::DeviceMemory> Buffer::create_buffer(const vk::MemoryPropertyFlags mem_props,
        const vk::BufferUsageFlags usage) const {
        vk::BufferCreateInfo buffer_create_info = {};
        buffer_create_info.size = m_size;
        buffer_create_info.usage = usage;
        buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
        buffer_create_info.queueFamilyIndexCount = 0;
        buffer_create_info.pQueueFamilyIndices = nullptr;
        auto buffer = m_device->get().createBuffer(buffer_create_info);

        // allocate memory
        const auto memory_required = m_device->get().getBufferMemoryRequirements(buffer);
        vk::MemoryAllocateInfo allocate_info = {};
        allocate_info.allocationSize = memory_required.size;
        allocate_info.memoryTypeIndex = m_device->get_memory_index(mem_props, memory_required.memoryTypeBits);
        auto memory = m_device->get().allocateMemory(allocate_info);
        m_device->get().bindBufferMemory(buffer, memory, 0);
        return std::make_pair(buffer, memory);
    }

    Buffer::~Buffer() {
        m_device->get().destroyBuffer(m_buffer);
        m_device->get().freeMemory(m_memory);
    }

    void Buffer::write(const void* data) {
        if (!data || m_buffer_type == BufferMemoryType::DeviceLocal) {
            LOG_ERROR("Buffer is not host visible or data is null");
        }

        void* mapping = m_device->get().mapMemory(m_memory, 0, vk::WholeSize);
        std::memcpy(mapping, data, m_size);
        m_device->get().unmapMemory(m_memory);
    }
}
