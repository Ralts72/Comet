#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;
    enum class BufferMemoryType {
        HostVisible,    // CPU 可直接访问，适合频繁更新
        DeviceLocal     // GPU 专用高速内存，适合静态数据
    };

    class Buffer {
    public:
        Buffer(Device* device, vk::BufferUsageFlags usage, size_t size, const void *data = nullptr,
            BufferMemoryType buffer_type = BufferMemoryType::DeviceLocal);
        ~Buffer();
        void write(const void *data);

        [[nodiscard]] vk::Buffer get() const { return m_buffer; }
        [[nodiscard]] vk::DeviceMemory get_memory() const { return m_memory; }
        [[nodiscard]] size_t get_size() const { return m_size; }
        [[nodiscard]] BufferMemoryType get_buffer_memory_type() const { return m_buffer_type; }

    private:
        std::pair<vk::Buffer, vk::DeviceMemory> create_buffer(vk::MemoryPropertyFlags mem_props, vk::BufferUsageFlags usage) const;

        Device* m_device;
        vk::Buffer m_buffer;
        vk::DeviceMemory m_memory;
        size_t m_size;
        BufferMemoryType m_buffer_type;
    };
}
