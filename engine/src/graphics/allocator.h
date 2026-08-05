#pragma once

#include "common/export.h"
#include "vk_common.h"

#include <string_view>
#include <utility>

#include <vk_mem_alloc.h>

namespace Comet {
    enum class AllocationUsage {
        Device,
        Upload,
        CpuToGpu,
        Readback
    };

    struct AllocationCreateInfo {
        AllocationUsage usage = AllocationUsage::Device;
        bool persistent_mapping = false;
        std::string_view debug_name;
    };

    class Allocation final {
    public:
        Allocation() = default;

        Allocation(const Allocation&) = delete;
        Allocation& operator=(const Allocation&) = delete;

        Allocation(Allocation&& other) noexcept
            : m_handle(std::exchange(other.m_handle, VK_NULL_HANDLE)) {}

        Allocation& operator=(Allocation&& other) noexcept {
            if(this != &other) {
                m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);
            }
            return *this;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return m_handle != VK_NULL_HANDLE;
        }

    private:
        friend class Allocator;

        VmaAllocation m_handle = VK_NULL_HANDLE;
    };

    class COMET_API Allocator final {
    public:
        struct CreateInfo {
            vk::Instance instance;
            vk::PhysicalDevice physical_device;
            vk::Device device;
            uint32_t vulkan_api_version = VK_API_VERSION_1_3;
        };

        struct BufferAllocation {
            vk::Buffer buffer = VK_NULL_HANDLE;
            Allocation allocation;
            void* mapped_data = nullptr;
        };

        struct ImageAllocation {
            vk::Image image = VK_NULL_HANDLE;
            Allocation allocation;
            void* mapped_data = nullptr;
        };

        explicit Allocator(const CreateInfo& create_info);

        ~Allocator();

        Allocator(const Allocator&) = delete;
        Allocator& operator=(const Allocator&) = delete;
        Allocator(Allocator&&) noexcept = delete;
        Allocator& operator=(Allocator&&) noexcept = delete;

        [[nodiscard]] BufferAllocation create_buffer(
            const vk::BufferCreateInfo& buffer_info,
            const AllocationCreateInfo& allocation_info = {}) const;

        void destroy_buffer(vk::Buffer buffer, Allocation& allocation) const;

        [[nodiscard]] ImageAllocation create_image(
            const vk::ImageCreateInfo& image_info,
            const AllocationCreateInfo& allocation_info = {}) const;

        void destroy_image(vk::Image image, Allocation& allocation) const;

        [[nodiscard]] void* map_memory(const Allocation& allocation) const;

        void unmap_memory(const Allocation& allocation) const;

        void flush_memory(const Allocation& allocation, size_t offset, size_t size) const;

    private:
        VmaAllocator m_allocator = VK_NULL_HANDLE;
    };
}
