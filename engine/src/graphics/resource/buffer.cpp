#include "graphics/resource/buffer.h"

#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "graphics/device.h"

#include <cstring>

namespace Comet {
    namespace {
        vk::BufferCreateInfo build_buffer_create_info(
            const Flags<BufferUsage> usage,
            const size_t size) {
            vk::BufferCreateInfo create_info{};
            create_info.size = size;
            create_info.usage = Graphics::buffer_usage_to_vk(usage);
            create_info.sharingMode = vk::SharingMode::eExclusive;
            return create_info;
        }
    }

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

    Allocator& Buffer::get_allocator() const {
        return m_device.get_allocator();
    }

    GPUBuffer::GPUBuffer(Device& device,
                         const size_t size,
                         Allocator::BufferAllocation allocation)
        : Buffer(device, size) {
        PROFILE_SCOPE("Buffer::Constructor");
        m_buffer = allocation.buffer;
        m_allocation = std::move(allocation.allocation);
    }

    CPUBuffer::CPUBuffer(Device& device,
                         const size_t size,
                         Allocator::BufferAllocation allocation)
        : Buffer(device, size) {
        m_buffer = allocation.buffer;
        m_mapped_data = allocation.mapped_data;
        m_allocation = std::move(allocation.allocation);
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
        auto attempt = try_create_cpu_buffer(
            device, usage, size, false, data, debug_name);
        if(!attempt) {
            LOG_FATAL("Failed to create CPU buffer '{}' ({} bytes): {}",
                debug_name,
                size,
                vk::to_string(attempt.result()));
        }
        return std::move(attempt).value();
    }

    GpuResourceResult<std::shared_ptr<CPUBuffer>>
    Buffer::try_create_cpu_buffer(
        Device& device,
        const Flags<BufferUsage> usage,
        const size_t size,
        const bool within_budget,
        const void* data,
        const std::string_view debug_name) {
        return try_create_mapped_buffer(
            device,
            usage,
            size,
            AllocationUsage::CpuToGpu,
            within_budget,
            data,
            debug_name);
    }

    std::shared_ptr<CPUBuffer> Buffer::create_upload_buffer(
        Device& device,
        const Flags<BufferUsage> usage,
        const size_t size,
        const void* data,
        const std::string_view debug_name) {
        auto attempt = try_create_upload_buffer(
            device, usage, size, false, data, debug_name);
        if(!attempt) {
            LOG_FATAL("Failed to create upload buffer '{}' ({} bytes): {}",
                debug_name,
                size,
                vk::to_string(attempt.result()));
        }
        return std::move(attempt).value();
    }

    GpuResourceResult<std::shared_ptr<CPUBuffer>>
    Buffer::try_create_upload_buffer(
        Device& device,
        const Flags<BufferUsage> usage,
        const size_t size,
        const bool within_budget,
        const void* data,
        const std::string_view debug_name) {
        return try_create_mapped_buffer(
            device,
            usage,
            size,
            AllocationUsage::Upload,
            within_budget,
            data,
            debug_name);
    }

    GpuResourceResult<std::shared_ptr<CPUBuffer>>
    Buffer::try_create_mapped_buffer(
        Device& device,
        const Flags<BufferUsage> usage,
        const size_t size,
        const AllocationUsage allocation_usage,
        const bool within_budget,
        const void* data,
        const std::string_view debug_name) {
        if(size == 0) {
            LOG_FATAL("Buffer size must be greater than zero");
        }
        if(allocation_usage != AllocationUsage::Upload
           && allocation_usage != AllocationUsage::CpuToGpu) {
            LOG_FATAL("CPUBuffer requires Upload or CpuToGpu allocation usage");
        }

        const std::string_view resolved_name = debug_name.empty()
            ? "CPU buffer"
            : debug_name;
        auto allocation = device.get_allocator().try_create_buffer(
            build_buffer_create_info(usage, size),
            {
                .usage = allocation_usage,
                .persistent_mapping = true,
                .within_budget = within_budget,
                .debug_name = resolved_name
            });
        if(!allocation) {
            return GpuResourceResult<std::shared_ptr<CPUBuffer>>::failure(
                allocation.result());
        }

        std::shared_ptr<CPUBuffer> buffer(new CPUBuffer(
            device,
            size,
            std::move(allocation).value()));
        if(data) {
            buffer->write(data);
        }
        return GpuResourceResult<std::shared_ptr<CPUBuffer>>::success(
            std::move(buffer));
    }

    std::shared_ptr<Buffer> Buffer::create_gpu_buffer(
        Device& device,
        const Flags<BufferUsage> usage,
        const size_t size,
        const std::string_view debug_name) {
        auto attempt = try_create_gpu_buffer(
            device, usage, size, false, debug_name);
        if(!attempt) {
            LOG_FATAL("Failed to create GPU buffer '{}' ({} bytes): {}",
                debug_name,
                size,
                vk::to_string(attempt.result()));
        }
        return std::move(attempt).value();
    }

    GpuResourceResult<std::shared_ptr<Buffer>>
    Buffer::try_create_gpu_buffer(
        Device& device,
        const Flags<BufferUsage> usage,
        const size_t size,
        const bool within_budget,
        const std::string_view debug_name) {
        if(size == 0) {
            LOG_FATAL("Buffer size must be greater than zero");
        }

        const std::string_view resolved_name = debug_name.empty()
            ? "GPU buffer"
            : debug_name;
        auto allocation = device.get_allocator().try_create_buffer(
            build_buffer_create_info(usage | BufferUsage::CopyDst, size),
            {
                .usage = AllocationUsage::Device,
                .within_budget = within_budget,
                .debug_name = resolved_name
            });
        if(!allocation) {
            return GpuResourceResult<std::shared_ptr<Buffer>>::failure(
                allocation.result());
        }

        std::shared_ptr<Buffer> buffer(new GPUBuffer(
            device,
            size,
            std::move(allocation).value()));
        return GpuResourceResult<std::shared_ptr<Buffer>>::success(
            std::move(buffer));
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
