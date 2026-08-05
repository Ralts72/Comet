#pragma once

#include <vulkan/vulkan.hpp>

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Comet {
    struct DeviceCapabilityRequest {
        float max_sampler_anisotropy = 1.0f;
    };

    struct DeviceCapability {
        std::vector<const char*> enabled_extensions;
        vk::PhysicalDeviceFeatures enabled_features{};
        float max_sampler_anisotropy = 1.0f;
    };

    [[nodiscard]] std::vector<const char*> get_available_names(
        const std::vector<const char*>& requested_names,
        const std::set<std::string>& available_names,
        std::string_view item_type);

    [[nodiscard]] DeviceCapability select_device_capabilities(
        const DeviceCapabilityRequest& request,
        vk::PhysicalDevice physical_device);
}
