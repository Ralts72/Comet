#include "vk_allocator.h"
#include "common/logger.h"

namespace Comet {
    VulkanAllocator::VulkanAllocator(const CreateInfo& create_info) {
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

    VulkanAllocator::~VulkanAllocator() {
        if(m_allocator) {
            vmaDestroyAllocator(m_allocator);
        }
    }

    VulkanAllocator::BufferAllocation VulkanAllocator::create_buffer(
        const vk::BufferCreateInfo& buffer_info,
        const vk::MemoryPropertyFlags memory_properties) const {
        VmaAllocationCreateInfo allocation_info = {};
        allocation_info.requiredFlags = static_cast<VkMemoryPropertyFlags>(memory_properties);

        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        const VkBufferCreateInfo& vk_buffer_info = buffer_info;
        const VkResult result = vmaCreateBuffer(m_allocator, &vk_buffer_info, &allocation_info, &buffer, &allocation, nullptr);
        if(result != VK_SUCCESS) {
            LOG_FATAL("Failed to create VMA buffer: {}", vk::to_string(static_cast<vk::Result>(result)));
        }

        return {vk::Buffer(buffer), allocation};
    }

    void VulkanAllocator::destroy_buffer(const vk::Buffer buffer, const VmaAllocation allocation) const {
        if(buffer && allocation) {
            vmaDestroyBuffer(m_allocator, static_cast<VkBuffer>(buffer), allocation);
        }
    }

    VulkanAllocator::ImageAllocation VulkanAllocator::create_image(
        const vk::ImageCreateInfo& image_info,
        const vk::MemoryPropertyFlags memory_properties) const {
        VmaAllocationCreateInfo allocation_info = {};
        allocation_info.requiredFlags = static_cast<VkMemoryPropertyFlags>(memory_properties);

        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        const VkImageCreateInfo& vk_image_info = image_info;
        const VkResult result = vmaCreateImage(m_allocator, &vk_image_info, &allocation_info, &image, &allocation, nullptr);
        if(result != VK_SUCCESS) {
            LOG_FATAL("Failed to create VMA image: {}", vk::to_string(static_cast<vk::Result>(result)));
        }

        return {vk::Image(image), allocation};
    }

    void VulkanAllocator::destroy_image(const vk::Image image, const VmaAllocation allocation) const {
        if(image && allocation) {
            vmaDestroyImage(m_allocator, static_cast<VkImage>(image), allocation);
        }
    }

    void* VulkanAllocator::map_memory(const VmaAllocation allocation) const {
        if(!allocation) {
            LOG_FATAL("Cannot map null VMA allocation");
        }

        void* mapping = nullptr;
        const VkResult result = vmaMapMemory(m_allocator, allocation, &mapping);
        if(result != VK_SUCCESS) {
            LOG_FATAL("Failed to map VMA allocation: {}", vk::to_string(static_cast<vk::Result>(result)));
        }

        return mapping;
    }

    void VulkanAllocator::unmap_memory(const VmaAllocation allocation) const {
        if(!allocation) {
            LOG_FATAL("Cannot unmap null VMA allocation");
        }

        vmaUnmapMemory(m_allocator, allocation);
    }
}
