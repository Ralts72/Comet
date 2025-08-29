#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;

    class Buffer {
    public:
        enum class Type { HostVisible, DeviceLocal };
        Buffer(Device* device, Flags<BufferUsage> usage, size_t size, const void *data, Type buffer_type);
        virtual ~Buffer();

        static std::shared_ptr<Buffer> create_cpu_buffer(Device* device, Flags<BufferUsage> usage, size_t size, const void *data = nullptr);
        static std::shared_ptr<Buffer> create_gpu_buffer(Device* device, Flags<BufferUsage> usage, size_t size, const void *data = nullptr);

        [[nodiscard]] vk::Buffer get() const { return m_buffer; }
        [[nodiscard]] vk::DeviceMemory get_memory() const { return m_memory; }
        [[nodiscard]] size_t get_size() const { return m_size; }
        [[nodiscard]] Type get_buffer_memory_type() const { return m_buffer_type; }

    protected:
        [[nodiscard]] std::pair<vk::Buffer, vk::DeviceMemory> create_buffer(Flags<MemoryType> mem_props,
            Flags<BufferUsage> usage) const;

        Device* m_device;
        vk::Buffer m_buffer;
        vk::DeviceMemory m_memory;
        size_t m_size;
        Type m_buffer_type;
    };

    class GPUBuffer final: public Buffer {
    public:
        GPUBuffer(Device* device, Flags<BufferUsage> usage, size_t size, const void *data);
    };

    class CPUBuffer final: public Buffer {
    public:
        CPUBuffer(Device* device, Flags<BufferUsage> usage, size_t size, const void *data);
        void write(const void* data) const;
    };
}
