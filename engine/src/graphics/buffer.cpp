#include "buffer.h"

#include "command_context.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "device.h"

#include <cstring>
#include <string>

namespace Comet {
    Buffer::Buffer(Device& device, const size_t size)
        : m_device(device), m_size(size) {
        if(size == 0) {
            LOG_FATAL("Buffer size must be greater than zero");
        }
    }

    Buffer::~Buffer() {
        if(m_buffer && m_allocation) {
            get_allocator().destroy_buffer(m_buffer, m_allocation);
        }
    }

    Allocator::BufferAllocation Buffer::create_buffer(
        const Flags<BufferUsage> usage,
        const AllocationCreateInfo& allocation_info) const {
        vk::BufferCreateInfo buffer_create_info = {};
        buffer_create_info.size = m_size;
        buffer_create_info.usage = Graphics::buffer_usage_to_vk(usage);
        buffer_create_info.sharingMode = vk::SharingMode::eExclusive;

        return get_allocator().create_buffer(buffer_create_info, allocation_info);
    }

    Allocator& Buffer::get_allocator() const {
        return m_device.get_allocator();
    }

    GPUBuffer::GPUBuffer(Device& device,
                         const Flags<BufferUsage> usage,
                         const size_t size,
                         const void* data,
                         const std::string_view debug_name)
        : Buffer(device, size) {
        PROFILE_SCOPE("Buffer::Constructor");
        if(!data) {
            LOG_FATAL("GPUBuffer requires initial data");
        }

        const std::string_view resolved_name = debug_name.empty() ? "GPU buffer" : debug_name;
        std::string upload_name(resolved_name);
        upload_name += " upload";

        auto [buffer, allocation, mapped_data] = create_buffer(
            Flags<BufferUsage>(BufferUsage::CopySrc),
            {
                .usage = AllocationUsage::Upload,
                .persistent_mapping = true,
                .debug_name = upload_name
            });
        std::memcpy(mapped_data, data, m_size);
        get_allocator().flush_memory(allocation, 0, m_size);

        auto device_buffer = create_buffer(
            usage | BufferUsage::CopyDst,
            {
                .usage = AllocationUsage::Device,
                .debug_name = resolved_name
        });
        m_buffer = device_buffer.buffer;
        m_allocation = std::move(device_buffer.allocation);

        const auto context = m_device.create_command_context();
        context->copy_buffer(buffer, m_buffer, m_size);
        context->submit_and_wait();

        get_allocator().destroy_buffer(buffer, allocation);
    }

    CPUBuffer::CPUBuffer(Device& device,
                         const Flags<BufferUsage> usage,
                         const size_t size,
                         const void* data,
                         const AllocationUsage allocation_usage,
                         const std::string_view debug_name)
        : Buffer(device, size) {
        if(allocation_usage != AllocationUsage::Upload
            && allocation_usage != AllocationUsage::CpuToGpu) {
            LOG_FATAL("CPUBuffer requires Upload or CpuToGpu allocation usage");
        }

        const std::string_view resolved_name = debug_name.empty() ? "CPU buffer" : debug_name;
        auto [buffer, allocation, mapped_data] = create_buffer(
            usage,
            {
                .usage = allocation_usage,
                .persistent_mapping = true,
                .debug_name = resolved_name
        });
        m_buffer = buffer;
        m_allocation = std::move(allocation);
        m_mapped_data = mapped_data;

        if(data) {
            write(data);
        }
    }

    CPUBuffer::~CPUBuffer() {
        m_mapped_data = nullptr;
    }

    std::shared_ptr<Buffer> Buffer::create_cpu_buffer(
        Device& device,
        const Flags<BufferUsage> usage,
        const size_t size,
        const void* data,
        const std::string_view debug_name) {
        return std::make_shared<CPUBuffer>(
            device, usage, size, data, AllocationUsage::CpuToGpu, debug_name);
    }

    std::shared_ptr<Buffer> Buffer::create_upload_buffer(
        Device& device,
        const Flags<BufferUsage> usage,
        const size_t size,
        const void* data,
        const std::string_view debug_name) {
        if(!data) {
            LOG_FATAL("Upload buffer requires initial data");
        }
        return std::make_shared<CPUBuffer>(
            device, usage, size, data, AllocationUsage::Upload, debug_name);
    }

    std::shared_ptr<Buffer> Buffer::create_gpu_buffer(
        Device& device,
        const Flags<BufferUsage> usage,
        const size_t size,
        const void* data,
        const std::string_view debug_name) {
        if(!data) {
            LOG_FATAL("GPUBuffer requires initial data");
        }
        return std::make_shared<GPUBuffer>(device, usage, size, data, debug_name);
    }

    void CPUBuffer::write(const void* data) const {
        write(data, m_size, 0);
    }

    void CPUBuffer::write(const void* data, const size_t size, const size_t offset) const {
        if(!data) {
            LOG_ERROR("CPUBuffer write data must not be null");
            return;
        }
        if(size == 0) {
            LOG_ERROR("CPUBuffer write size must be greater than zero");
            return;
        }
        if(offset > m_size || size > m_size - offset) {
            LOG_ERROR("CPUBuffer write range [{}..{}) exceeds buffer size {}",
                offset, offset + size, m_size);
            return;
        }
        if(!m_mapped_data) {
            LOG_FATAL("CPUBuffer has no mapped memory");
        }

        auto* destination = static_cast<std::byte*>(m_mapped_data) + offset;
        std::memcpy(destination, data, size);
        get_allocator().flush_memory(m_allocation, offset, size);
    }
}
