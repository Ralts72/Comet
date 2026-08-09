#pragma once

#include "allocator.h"
#include "common/export.h"
#include "vk_common.h"

#include <string_view>

namespace Comet {
    class Device;

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

        static std::shared_ptr<Buffer> create_upload_buffer(
            Device& device,
            Flags<BufferUsage> usage,
            size_t size,
            const void* data,
            std::string_view debug_name = {});

        static std::shared_ptr<Buffer> create_gpu_buffer(
            Device& device,
            Flags<BufferUsage> usage,
            size_t size,
            const void* data,
            std::string_view debug_name = {});

        [[nodiscard]] vk::Buffer get() const { return m_buffer; }
        [[nodiscard]] size_t get_size() const { return m_size; }

    protected:
        [[nodiscard]] Allocator::BufferAllocation create_buffer(
            Flags<BufferUsage> usage,
            const AllocationCreateInfo& allocation_info) const;

        [[nodiscard]] Allocator& get_allocator() const;

        Device& m_device;
        vk::Buffer m_buffer = VK_NULL_HANDLE;
        Allocation m_allocation;
        size_t m_size;
    };

    class COMET_API GPUBuffer final: public Buffer {
    public:
        GPUBuffer(Device& device,
                  Flags<BufferUsage> usage,
                  size_t size,
                  const void* data,
                  std::string_view debug_name);
    };

    class COMET_API CPUBuffer final: public Buffer {
    public:
        CPUBuffer(Device& device,
                  Flags<BufferUsage> usage,
                  size_t size,
                  const void* data,
                  AllocationUsage allocation_usage = AllocationUsage::CpuToGpu,
                  std::string_view debug_name = {});

        ~CPUBuffer() override;

        void write(const void* data) const;

        void write(const void* data, size_t size, size_t offset = 0) const;

    private:
        void* m_mapped_data = nullptr;
    };
}
