#include "buffer.h"
#include "command_buffer.h"
#include "device.h"
#include "queue.h"
#include "common/profiler.h"

namespace Comet {
    Buffer::Buffer(Device* device, const Flags<BufferUsage> usage, const size_t size, const void* data,
        const Type buffer_type) : m_device(device), m_size(size), m_buffer_type(buffer_type) {}

    Buffer::~Buffer() {
        m_device->get().destroyBuffer(m_buffer);
        m_device->get().freeMemory(m_memory);
    }

    std::pair<vk::Buffer, vk::DeviceMemory> Buffer::create_buffer(const Flags<MemoryType> mem_props,
        const Flags<BufferUsage> usage) const {
        vk::BufferCreateInfo buffer_create_info = {};
        buffer_create_info.size = m_size;
        buffer_create_info.usage = Graphics::buffer_usage_to_vk(usage);
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

    GPUBuffer::GPUBuffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data)
    : Buffer(device, usage, size, data, Type::DeviceLocal) {
        PROFILE_SCOPE("Buffer::Constructor");
        auto [stage_buffer, stage_memory] = create_buffer(Flags<MemoryType>(MemoryType::CPULocal)
            | Flags<MemoryType>(MemoryType::Coherence), Flags<BufferUsage>(BufferUsage::CopySrc));
        void* mapping = m_device->get().mapMemory(stage_memory, 0, vk::WholeSize);
        memcpy(mapping, data, m_size);
        m_device->get().unmapMemory(stage_memory);
        std::tie(m_buffer, m_memory) = create_buffer(Flags<MemoryType>(MemoryType::GPULocal),
            usage | Flags<BufferUsage>(BufferUsage::CopyDst));

        m_device->copy_buffer(stage_buffer, m_buffer, m_size);

        m_device->get().destroyBuffer(stage_buffer);
        m_device->get().freeMemory(stage_memory);
    }

    CPUBuffer::CPUBuffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data)
    : Buffer(device, usage, size, data, Type::HostVisible) {
        std::tie(m_buffer, m_memory) = create_buffer(Flags<MemoryType>(MemoryType::CPULocal)
            | Flags<MemoryType>(MemoryType::Coherence), usage);
        if (data) {
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
        if (!data) {
            LOG_ERROR("Buffer is not host visible or data is null");
            return;
        }

        void* mapping = m_device->get().mapMemory(m_memory, 0, vk::WholeSize);
        std::memcpy(mapping, data, m_size);
        m_device->get().unmapMemory(m_memory);
    }
}
