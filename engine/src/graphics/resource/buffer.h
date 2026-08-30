#pragma once

#include "graphics/resource/allocator.h"
#include "common/export.h"
#include "graphics/vk_common.h"

#include <string_view>

namespace Comet {
    class Device;
    class CPUBuffer;

    class COMET_API Buffer {
    public:
        Buffer(Device& device, size_t size);

        virtual ~Buffer();

        Buffer(const Buffer&) = delete;

        Buffer& operator=(const Buffer&) = delete;

        Buffer(Buffer&&) noexcept = delete;

        Buffer& operator=(Buffer&&) noexcept = delete;

        static std::shared_ptr<Buffer> create_cpu_buffer(
            Device& device,
            Flags<BufferUsage> usage,
            size_t size,
            const void* data = nullptr,
            std::string_view debug_name = {});

        static std::shared_ptr<CPUBuffer> create_upload_buffer(
            Device& device,
            Flags<BufferUsage> usage,
            size_t size,
            const void* data = nullptr,
            std::string_view debug_name = {});

        static GpuResourceResult<std::shared_ptr<CPUBuffer>>
        try_create_upload_buffer(
            Device& device,
            Flags<BufferUsage> usage,
            size_t size,
            bool within_budget,
            const void* data = nullptr,
            std::string_view debug_name = {});

        static std::shared_ptr<Buffer> create_gpu_buffer(
            Device& device,
            Flags<BufferUsage> usage,
            size_t size,
            std::string_view debug_name = {});

        static GpuResourceResult<std::shared_ptr<Buffer>>
        try_create_gpu_buffer(
            Device& device,
            Flags<BufferUsage> usage,
            size_t size,
            bool within_budget,
            std::string_view debug_name = {});

        [[nodiscard]] vk::Buffer get() const { return m_buffer; }
        [[nodiscard]] size_t get_size() const { return m_size; }

    protected:
        [[nodiscard]] Allocator& get_allocator() const;

        Device& m_device;
        vk::Buffer m_buffer = VK_NULL_HANDLE;
        Allocation m_allocation;
        size_t m_size;

    private:
        static GpuResourceResult<std::shared_ptr<CPUBuffer>>
        try_create_mapped_buffer(
            Device& device,
            Flags<BufferUsage> usage,
            size_t size,
            AllocationUsage allocation_usage,
            bool within_budget,
            const void* data,
            std::string_view debug_name);
    };

    class COMET_API GPUBuffer final: public Buffer {
    private:
        friend class Buffer;

        GPUBuffer(Device& device,
                  size_t size,
                  Allocator::BufferAllocation allocation);
    };

    class COMET_API CPUBuffer final: public Buffer {
    public:
        ~CPUBuffer() override;

        void write(const void* data) const;

        void write(const void* data, size_t size, size_t offset = 0) const;

    private:
        friend class Buffer;

        CPUBuffer(Device& device,
                  size_t size,
                  Allocator::BufferAllocation allocation);

        void* m_mapped_data = nullptr;
    };
}
