#pragma once
#include "vk_common.h"
#include "common/export.h"
#include <vk_mem_alloc.h>

namespace Comet {
    class COMET_API VulkanAllocator final {
    public:
        struct CreateInfo {
            vk::Instance instance;
            vk::PhysicalDevice physical_device;
            vk::Device device;
            uint32_t vulkan_api_version = VK_API_VERSION_1_3;
        };

        struct BufferAllocation {
            vk::Buffer buffer = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
        };

        struct ImageAllocation {
            vk::Image image = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
        };

        explicit VulkanAllocator(const CreateInfo& create_info);

        ~VulkanAllocator();

        VulkanAllocator(const VulkanAllocator&) = delete;

        VulkanAllocator& operator=(const VulkanAllocator&) = delete;

        VulkanAllocator(VulkanAllocator&&) noexcept = delete;

        VulkanAllocator& operator=(VulkanAllocator&&) noexcept = delete;

        [[nodiscard]] BufferAllocation create_buffer(const vk::BufferCreateInfo& buffer_info,
                                                     vk::MemoryPropertyFlags memory_properties) const;

        void destroy_buffer(vk::Buffer buffer, VmaAllocation allocation) const;

        [[nodiscard]] ImageAllocation create_image(const vk::ImageCreateInfo& image_info,
                                                   vk::MemoryPropertyFlags memory_properties) const;

        void destroy_image(vk::Image image, VmaAllocation allocation) const;

        [[nodiscard]] void* map_memory(VmaAllocation allocation) const;

        void unmap_memory(VmaAllocation allocation) const;

    private:
        VmaAllocator m_allocator = VK_NULL_HANDLE;
    };
}
