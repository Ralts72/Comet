#pragma once

#include "common/export.h"
#include "graphics/resource/memory_budget.h"
#include "graphics/vk_common.h"

#include <optional>
#include <string_view>
#include <utility>

#include <vk_mem_alloc.h>

namespace Comet {
    template<typename T>
    class ResourceAllocationResult {
    public:
        ResourceAllocationResult() = default;

        [[nodiscard]] static ResourceAllocationResult success(T value) {
            return ResourceAllocationResult(
                std::optional<T>(std::move(value)),
                vk::Result::eSuccess);
        }

        [[nodiscard]] static ResourceAllocationResult failure(
            const vk::Result result) {
            return ResourceAllocationResult(
                std::nullopt,
                result == vk::Result::eSuccess
                    ? vk::Result::eErrorUnknown
                    : result);
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return m_value.has_value();
        }

        [[nodiscard]] T& value() & { return m_value.value(); }
        [[nodiscard]] const T& value() const & { return m_value.value(); }
        [[nodiscard]] T&& value() && { return std::move(m_value).value(); }

        [[nodiscard]] vk::Result result() const noexcept { return m_result; }

    private:
        ResourceAllocationResult(
            std::optional<T> value,
            const vk::Result result)
            : m_value(std::move(value)), m_result(result) {}

        std::optional<T> m_value;
        vk::Result m_result = vk::Result::eErrorUnknown;
    };

    enum class AllocationUsage {
        Device,
        Upload,
        CpuToGpu,
        Readback
    };

    struct AllocationCreateInfo {
        AllocationUsage usage = AllocationUsage::Device;
        bool persistent_mapping = false;
        bool within_budget = false;
        std::string_view debug_name;
    };

    class Allocation final {
    public:
        Allocation() = default;

        Allocation(const Allocation&) = delete;
        Allocation& operator=(const Allocation&) = delete;

        Allocation(Allocation&& other) noexcept
            : m_handle(std::exchange(other.m_handle, VK_NULL_HANDLE)) {}

        Allocation& operator=(Allocation&& other) noexcept;

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
            bool memory_budget_enabled = false;
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

        [[nodiscard]] ResourceAllocationResult<BufferAllocation>
        try_create_buffer(
            const vk::BufferCreateInfo& buffer_info,
            const AllocationCreateInfo& allocation_info = {}) const;

        void destroy_buffer(vk::Buffer buffer, Allocation& allocation) const;

        [[nodiscard]] ImageAllocation create_image(
            const vk::ImageCreateInfo& image_info,
            const AllocationCreateInfo& allocation_info = {}) const;

        [[nodiscard]] ResourceAllocationResult<ImageAllocation>
        try_create_image(
            const vk::ImageCreateInfo& image_info,
            const AllocationCreateInfo& allocation_info = {}) const;

        void destroy_image(vk::Image image, Allocation& allocation) const;

        [[nodiscard]] void* map_memory(const Allocation& allocation) const;

        void unmap_memory(const Allocation& allocation) const;

        void flush_memory(const Allocation& allocation, size_t offset, size_t size) const;

        void set_current_frame_index(uint64_t frame_serial) const;

        [[nodiscard]] MemoryBudgetSnapshot query_memory_budget() const;

    private:
        VmaAllocator m_allocator = VK_NULL_HANDLE;
        bool m_memory_budget_enabled = false;
    };
}
