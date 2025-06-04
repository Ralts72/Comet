#pragma once
#include "../pch.h"

namespace Comet {
    class Device;

    class MemoryRes {
    public:
        MemoryRes(Device&, uint64_t size, uint32_t memory_type_index);

        MemoryRes(const MemoryRes&) = delete;

        MemoryRes(MemoryRes&&) = delete;

        MemoryRes& operator=(const MemoryRes&) = delete;

        MemoryRes& operator=(MemoryRes&&) = delete;

        ~MemoryRes();

        size_t getSize() const noexcept { return m_size; }

        vk::DeviceMemory getVkMemory() const noexcept { return m_memory; }

    private:
        vk::DeviceMemory m_memory;
        Device& m_device;
        size_t m_size;
    };
}


