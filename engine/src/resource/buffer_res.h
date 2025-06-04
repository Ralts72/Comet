#pragma once
#include "../graphics/buffer.h"
#include "refcount.h"

namespace Comet {
    class Device;
    class MemoryRes;

    class BufferRes final: public Refcount {
    public:
        BufferRes(Device&, vk::PhysicalDevice, const Buffer::Descriptor&);

        ~BufferRes() override;

        BufferRes(const BufferRes&) = delete;

        BufferRes(BufferRes&&) = delete;

        BufferRes& operator=(const BufferRes&) = delete;

        BufferRes& operator=(BufferRes&&) = delete;

        enum Buffer::MapState getMapState() const { return m_map_state; }

        uint64_t getSize() const { return m_size; }

        void unmap();

        void mapAsync(uint64_t offset, uint64_t size);

        void mapAsync();

        [[nodiscard]] void* getMappedRange(uint64_t offset = 0) const;

        void flush() const;

        void flush(uint64_t offset, uint64_t size) const;

        void decrease() override;

        void buffData(void* data, size_t size, size_t offset);

        [[nodiscard]] vk::Buffer getVkBuffer() const { return m_buffer; }
        [[nodiscard]] MemoryRes* getMemory() const { return m_memory; }

    private:
        vk::MemoryPropertyFlags getMemoryProperty(vk::PhysicalDevice phyDevice, const Buffer::Descriptor&);

        void createBuffer(Device&, const Buffer::Descriptor&);

        void allocateMemory(Device&, vk::PhysicalDevice phyDevice, vk::MemoryPropertyFlags flags);

        vk::Buffer m_buffer;
        MemoryRes* m_memory{};

        Device& m_device;
        uint64_t m_size{};
        enum Buffer::MapState m_map_state = Buffer::MapState::Unmapped;
        void* m_map{};
        uint64_t m_mapped_offset{};
        uint64_t m_mapped_size{};
        bool m_is_mapping_coherence{};
    };
}
