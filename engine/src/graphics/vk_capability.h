#pragma once

#include "common/export.h"

#include <vulkan/vulkan.hpp>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Comet {
    struct QueueFamilyInfo {
        std::optional<uint32_t> queue_family_index;
        uint32_t queue_count = 0;
    };

    struct SwapchainRequest {
        uint32_t image_count = 3;
        vk::SurfaceFormatKHR surface_format{
            vk::Format::eB8G8R8A8Srgb,
            vk::ColorSpaceKHR::eSrgbNonlinear
        };
        vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eColorAttachment;
    };

    struct DeviceCapabilityRequest {
        SwapchainRequest swapchain;
        vk::Format depth_format = vk::Format::eD32Sfloat;
        vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1;
        float max_sampler_anisotropy = 1.0f;
    };

    struct DeviceCapability {
        vk::PhysicalDevice physical_device;
        QueueFamilyInfo graphics_queue_family;
        QueueFamilyInfo present_queue_family;
        std::vector<const char*> enabled_extensions;
        vk::PhysicalDeviceFeatures enabled_features{};
        float max_sampler_anisotropy = 1.0f;
    };

    struct SwapchainConfig {
        uint32_t image_count = 0;
        uint32_t image_layers = 1;
        vk::Extent2D extent;
        vk::SurfaceFormatKHR surface_format;
        vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;
        vk::SurfaceTransformFlagBitsKHR transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        vk::CompositeAlphaFlagBitsKHR composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eColorAttachment;
        bool clipped = true;
    };

    enum class SwapchainStatus {
        Ready,
        Deferred,
        Unsupported
    };

    struct SwapchainResult {
        SwapchainStatus status = SwapchainStatus::Unsupported;
        SwapchainConfig config;
        std::string message;
    };

    [[nodiscard]] std::vector<const char*> get_available_names(
        const std::vector<const char*>& requested_names,
        const std::set<std::string>& available_names,
        std::string_view item_type);

    [[nodiscard]] DeviceCapability select_physical_device(
        const std::vector<vk::PhysicalDevice>& physical_devices,
        vk::SurfaceKHR surface,
        const DeviceCapabilityRequest& request,
        uint32_t required_graphics_queue_count = 1,
        uint32_t required_present_queue_count = 1);

    [[nodiscard]] COMET_API SwapchainResult select_swapchain(
        const vk::SurfaceCapabilitiesKHR& capabilities,
        const std::vector<vk::SurfaceFormatKHR>& surface_formats,
        const std::vector<vk::PresentModeKHR>& present_modes,
        vk::Extent2D framebuffer_extent,
        const SwapchainRequest& request);
}
