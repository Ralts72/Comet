#pragma once

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

    struct DeviceCapabilityRequest {
        vk::SurfaceFormatKHR surface_format{
            vk::Format::eB8G8R8A8Srgb,
            vk::ColorSpaceKHR::eSrgbNonlinear
        };
        vk::Format depth_format = vk::Format::eD32Sfloat;
        vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1;
        vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;
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
}
