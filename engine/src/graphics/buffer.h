#pragma once
#include "vk_common.h"
#include <vk_mem_alloc.h>

namespace Comet {
    class Device;

    class Buffer {
    public:
        Buffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data);

        virtual ~Buffer();

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&&) noexcept = delete;
        Buffer& operator=(Buffer&&) noexcept = delete;

        static std::shared_ptr<Buffer> create_cpu_buffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data = nullptr);

        static std::shared_ptr<Buffer> create_gpu_buffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data = nullptr);

        [[nodiscard]] vk::Buffer get() const { return m_buffer; }
        [[nodiscard]] size_t get_size() const { return m_size; }

    protected:
        [[nodiscard]] std::pair<vk::Buffer, VmaAllocation> create_buffer(Flags<MemoryType> mem_props,
                                                                         Flags<BufferUsage> usage) const;

        Device* m_device;
        vk::Buffer m_buffer = VK_NULL_HANDLE;
        VmaAllocation m_allocation = VK_NULL_HANDLE;
        size_t m_size;
    };

    class GPUBuffer final: public Buffer {
    public:
        GPUBuffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data);
    };

    class CPUBuffer final: public Buffer {
    public:
        CPUBuffer(Device* device, Flags<BufferUsage> usage, size_t size, const void* data);

        void write(const void* data) const;
    };
}
