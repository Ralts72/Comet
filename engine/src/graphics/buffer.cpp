#include "buffer.h"
#include "command_buffer.h"
#include "device.h"
#include "common/profiler.h"
#include "command_context.h"
#include "vulkan_allocator.h"

namespace Comet {
    Buffer::Buffer(Device* device, const Flags<BufferUsage> usage, const size_t size, const void* data) : m_device(device), m_size(size) {}

    Buffer::~Buffer() {
        if(m_buffer && m_allocation) {
            get_allocator().destroy_buffer(m_buffer, m_allocation);
        }
    }

    std::pair<vk::Buffer, VmaAllocation> Buffer::create_buffer(const Flags<MemoryType> mem_props,
                                                               const Flags<BufferUsage> usage) const {
        vk::BufferCreateInfo buffer_create_info = {};
        buffer_create_info.size = m_size;
        buffer_create_info.usage = Graphics::buffer_usage_to_vk(usage);
        buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
        buffer_create_info.queueFamilyIndexCount = 0;
        buffer_create_info.pQueueFamilyIndices = nullptr;

        const auto allocation = get_allocator().create_buffer(
            buffer_create_info,
            Graphics::memory_property_to_vk(mem_props));

        return std::make_pair(allocation.buffer, allocation.allocation);
    }

    VulkanAllocator& Buffer::get_allocator() const {
        return m_device->get_allocator();
    }

    GPUBuffer::GPUBuffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data)
        : Buffer(device, usage, size, data) {
        PROFILE_SCOPE("Buffer::Constructor");
        auto [stage_buffer, stage_allocation] = create_buffer(Flags<MemoryType>(MemoryType::CPULocal)
                                                              | Flags<MemoryType>(MemoryType::Coherence), Flags<BufferUsage>(BufferUsage::CopySrc));
        void* mapping = get_allocator().map_memory(stage_allocation);
        memcpy(mapping, data, m_size);
        get_allocator().unmap_memory(stage_allocation);
        std::tie(m_buffer, m_allocation) = create_buffer(Flags<MemoryType>(MemoryType::GPULocal),
            usage | Flags<BufferUsage>(BufferUsage::CopyDst));

        auto ctx = m_device->create_command_context();
        ctx->copy_buffer(stage_buffer, m_buffer, m_size);
        ctx->submit_and_wait();

        get_allocator().destroy_buffer(stage_buffer, stage_allocation);
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

        void* mapping = get_allocator().map_memory(m_allocation);
        std::memcpy(mapping, data, m_size);
        get_allocator().unmap_memory(m_allocation);
    }
}
