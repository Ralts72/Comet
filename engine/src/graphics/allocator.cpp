#include "allocator.h"

#include "common/logger.h"

#include <string>

namespace Comet {
    namespace {
        const char* allocation_usage_name(const AllocationUsage usage) {
            switch(usage) {
                case AllocationUsage::Device:
                    return "device";
                case AllocationUsage::Upload:
                    return "upload";
                case AllocationUsage::CpuToGpu:
                    return "cpu-to-gpu";
                case AllocationUsage::Readback:
                    return "readback";
            }
            return "unknown";
        }

        VmaAllocationCreateInfo build_vma_allocation_info(const AllocationCreateInfo& create_info) {
            VmaAllocationCreateInfo vma_info = {};
            switch(create_info.usage) {
                case AllocationUsage::Device:
                    vma_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                    break;
                case AllocationUsage::Upload:
                    vma_info.usage = VMA_MEMORY_USAGE_AUTO;
                    vma_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
                    break;
                case AllocationUsage::CpuToGpu:
                    vma_info.usage = VMA_MEMORY_USAGE_AUTO;
                    vma_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
                    break;
                case AllocationUsage::Readback:
                    vma_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                    vma_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
                    break;
            }

            if(create_info.persistent_mapping) {
                if(create_info.usage == AllocationUsage::Device) {
                    LOG_FATAL("Device-only allocations cannot request persistent mapping");
                }
                vma_info.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
            }
            return vma_info;
        }

        void set_allocation_name(const VmaAllocator allocator,
                                 const VmaAllocation allocation,
                                 const std::string_view debug_name) {
            if(debug_name.empty()) {
                return;
            }

            const std::string name(debug_name);
            vmaSetAllocationName(allocator, allocation, name.c_str());
        }
    }

    Allocator::Allocator(const CreateInfo& create_info) {
        VmaAllocatorCreateInfo allocator_info = {};
        allocator_info.instance = create_info.instance;
        allocator_info.physicalDevice = create_info.physical_device;
        allocator_info.device = create_info.device;
        allocator_info.vulkanApiVersion = create_info.vulkan_api_version;

        const VkResult result = vmaCreateAllocator(&allocator_info, &m_allocator);
        if(result != VK_SUCCESS) {
            LOG_FATAL("Failed to create VMA allocator: {}", vk::to_string(static_cast<vk::Result>(result)));
        }
        LOG_INFO("VMA allocator created successfully");
    }

    Allocator::~Allocator() {
        if(m_allocator) {
            vmaDestroyAllocator(m_allocator);
        }
    }

    Allocator::BufferAllocation Allocator::create_buffer(
        const vk::BufferCreateInfo& buffer_info,
        const AllocationCreateInfo& allocation_info) const {
        const VmaAllocationCreateInfo vma_allocation_info = build_vma_allocation_info(allocation_info);

        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo created_info = {};
        const VkBufferCreateInfo& vk_buffer_info = buffer_info;
        const VkResult result = vmaCreateBuffer(
            m_allocator,
            &vk_buffer_info,
            &vma_allocation_info,
            &buffer,
            &allocation,
            &created_info);
        if(result != VK_SUCCESS) {
            LOG_FATAL("Failed to create VMA buffer '{}' ({} bytes, usage {}): {}",
                allocation_info.debug_name,
                buffer_info.size,
                allocation_usage_name(allocation_info.usage),
                vk::to_string(static_cast<vk::Result>(result)));
        }

        set_allocation_name(m_allocator, allocation, allocation_info.debug_name);
        if(allocation_info.persistent_mapping && !created_info.pMappedData) {
            vmaDestroyBuffer(m_allocator, buffer, allocation);
            LOG_FATAL("Persistently mapped buffer '{}' has no mapped pointer", allocation_info.debug_name);
        }

        Allocation wrapped_allocation;
        wrapped_allocation.m_handle = allocation;
        return {vk::Buffer(buffer), std::move(wrapped_allocation), created_info.pMappedData};
    }

    void Allocator::destroy_buffer(const vk::Buffer buffer, Allocation& allocation) const {
        if(buffer && allocation) {
            vmaDestroyBuffer(m_allocator, static_cast<VkBuffer>(buffer), allocation.m_handle);
            allocation.m_handle = VK_NULL_HANDLE;
        }
    }

    Allocator::ImageAllocation Allocator::create_image(
        const vk::ImageCreateInfo& image_info,
        const AllocationCreateInfo& allocation_info) const {
        const VmaAllocationCreateInfo vma_allocation_info = build_vma_allocation_info(allocation_info);

        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo created_info = {};
        const VkImageCreateInfo& vk_image_info = image_info;
        const VkResult result = vmaCreateImage(
            m_allocator,
            &vk_image_info,
            &vma_allocation_info,
            &image,
            &allocation,
            &created_info);
        if(result != VK_SUCCESS) {
            LOG_FATAL("Failed to create VMA image '{}' (usage {}): {}",
                allocation_info.debug_name,
                allocation_usage_name(allocation_info.usage),
                vk::to_string(static_cast<vk::Result>(result)));
        }

        set_allocation_name(m_allocator, allocation, allocation_info.debug_name);
        if(allocation_info.persistent_mapping && !created_info.pMappedData) {
            vmaDestroyImage(m_allocator, image, allocation);
            LOG_FATAL("Persistently mapped image '{}' has no mapped pointer", allocation_info.debug_name);
        }

        Allocation wrapped_allocation;
        wrapped_allocation.m_handle = allocation;
        return {vk::Image(image), std::move(wrapped_allocation), created_info.pMappedData};
    }

    void Allocator::destroy_image(const vk::Image image, Allocation& allocation) const {
        if(image && allocation) {
            vmaDestroyImage(m_allocator, static_cast<VkImage>(image), allocation.m_handle);
            allocation.m_handle = VK_NULL_HANDLE;
        }
    }

    void* Allocator::map_memory(const Allocation& allocation) const {
        if(!allocation) {
            LOG_FATAL("Cannot map null allocation");
        }

        void* mapping = nullptr;
        const VkResult result = vmaMapMemory(m_allocator, allocation.m_handle, &mapping);
        if(result != VK_SUCCESS) {
            LOG_FATAL("Failed to map allocation: {}", vk::to_string(static_cast<vk::Result>(result)));
        }
        return mapping;
    }

    void Allocator::unmap_memory(const Allocation& allocation) const {
        if(!allocation) {
            LOG_FATAL("Cannot unmap null allocation");
        }
        vmaUnmapMemory(m_allocator, allocation.m_handle);
    }

    void Allocator::flush_memory(const Allocation& allocation, const size_t offset, const size_t size) const {
        if(!allocation) {
            LOG_FATAL("Cannot flush null allocation");
        }

        const VkResult result = vmaFlushAllocation(m_allocator, allocation.m_handle, offset, size);
        if(result != VK_SUCCESS) {
            LOG_FATAL("Failed to flush allocation: {}", vk::to_string(static_cast<vk::Result>(result)));
        }
    }
}
