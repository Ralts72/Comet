#include "buffer.h"
#include "command_buffer.h"
#include "device.h"
#include "common/profiler.h"
#include "command_context.h"

namespace Comet {
    Buffer::Buffer(Device* device, const Flags<BufferUsage> usage, const size_t size, const void* data) : m_device(device), m_size(size) {}

    Buffer::~Buffer() {
        if(m_buffer && m_allocation) {
            vmaDestroyBuffer(m_device->get_allocator(), static_cast<VkBuffer>(m_buffer), m_allocation);
        }
    }

    std::pair<vk::Buffer, VmaAllocation> Buffer::create_buffer(const Flags<MemoryType> mem_props,
                                                               const Flags<BufferUsage> usage) const {
        VkBufferCreateInfo buffer_create_info = {};
        buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size = m_size;
        buffer_create_info.usage = static_cast<VkBufferUsageFlags>(Graphics::buffer_usage_to_vk(usage));
        buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocation_info = {};
        allocation_info.requiredFlags = static_cast<VkMemoryPropertyFlags>(Graphics::memory_property_to_vk(mem_props));

        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        const VkResult result = vmaCreateBuffer(
            m_device->get_allocator(), &buffer_create_info, &allocation_info, &buffer, &allocation, nullptr);
        if(result != VK_SUCCESS) {
            LOG_FATAL("Failed to create VMA buffer: {}", vk::to_string(static_cast<vk::Result>(result)));
        }

        return std::make_pair(vk::Buffer(buffer), allocation);
    }

    GPUBuffer::GPUBuffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data)
        : Buffer(device, usage, size, data) {
        PROFILE_SCOPE("Buffer::Constructor");
        auto [stage_buffer, stage_allocation] = create_buffer(Flags<MemoryType>(MemoryType::CPULocal)
                                                              | Flags<MemoryType>(MemoryType::Coherence), Flags<BufferUsage>(BufferUsage::CopySrc));
        void* mapping = nullptr;
        vmaMapMemory(m_device->get_allocator(), stage_allocation, &mapping);
        memcpy(mapping, data, m_size);
        vmaUnmapMemory(m_device->get_allocator(), stage_allocation);
        std::tie(m_buffer, m_allocation) = create_buffer(Flags<MemoryType>(MemoryType::GPULocal),
            usage | Flags<BufferUsage>(BufferUsage::CopyDst));

        auto ctx = m_device->create_command_context();
        ctx->copy_buffer(stage_buffer, m_buffer, m_size);
        ctx->submit_and_wait();

        vmaDestroyBuffer(m_device->get_allocator(), static_cast<VkBuffer>(stage_buffer), stage_allocation);
    }

    CPUBuffer::CPUBuffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data)
        : Buffer(device, usage, size, data) {
        std::tie(m_buffer, m_allocation) = create_buffer(Flags<MemoryType>(MemoryType::CPULocal)
                                                         | Flags<MemoryType>(MemoryType::Coherence), usage);
        if(data) {
            write(data);
        }
    }

    std::shared_ptr<Buffer> Buffer::create_cpu_buffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data) {
        return std::make_shared<CPUBuffer>(device, usage, size, data);
    }


    std::shared_ptr<Buffer> Buffer::create_gpu_buffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data) {
        return std::make_shared<GPUBuffer>(device, usage, size, data);
    }

    void CPUBuffer::write(const void* data) const {
        if(!data) {
            LOG_ERROR("Buffer is not host visible or data is null");
            return;
        }

        void* mapping = nullptr;
        vmaMapMemory(m_device->get_allocator(), m_allocation, &mapping);
        std::memcpy(mapping, data, m_size);
        vmaUnmapMemory(m_device->get_allocator(), m_allocation);
    }
}
