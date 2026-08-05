#include "vk_capability.h"

#include "common/logger.h"

#include <algorithm>
#include <cmath>

namespace Comet {
    namespace {
        const std::vector<const char*> s_requested_device_extensions = {
#ifdef __APPLE__
            "VK_KHR_portability_subset",
#endif
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };
    }

    std::vector<const char*> get_available_names(
        const std::vector<const char*>& requested_names,
        const std::set<std::string>& available_names,
        const std::string_view item_type) {
        std::vector<const char*> selection;
        selection.reserve(requested_names.size());

        for(const char* name : requested_names) {
            if(available_names.contains(name)) {
                selection.push_back(name);
                LOG_INFO("Enabled {}: {}", item_type, name);
            } else {
                LOG_WARN("Requested {} not supported and skipped: {}", item_type, name);
            }
        }

        return selection;
    }

    DeviceCapability select_device_capabilities(
        const DeviceCapabilityRequest& request,
        const vk::PhysicalDevice physical_device) {
        DeviceCapability capability;

        std::set<std::string> available_extensions;
        for(const auto& properties : physical_device.enumerateDeviceExtensionProperties()) {
            available_extensions.emplace(properties.extensionName);
        }
        capability.enabled_extensions = get_available_names(
            s_requested_device_extensions,
            available_extensions,
            "device extension");

        if(!std::isfinite(request.max_sampler_anisotropy)
            || request.max_sampler_anisotropy < 1.0f) {
            LOG_FATAL("Device sampler anisotropy request must be a finite number of at least 1.0");
        }
        if(request.max_sampler_anisotropy <= 1.0f) {
            LOG_INFO("Sampler anisotropy disabled by render configuration");
            return capability;
        }

        const auto supported_features = physical_device.getFeatures();
        if(!supported_features.samplerAnisotropy) {
            LOG_WARN("Sampler anisotropy {} requested but unsupported; falling back to 1",
                request.max_sampler_anisotropy);
            return capability;
        }

        capability.enabled_features.samplerAnisotropy = VK_TRUE;
        capability.max_sampler_anisotropy = std::min(
            request.max_sampler_anisotropy,
            physical_device.getProperties().limits.maxSamplerAnisotropy);

        if(capability.max_sampler_anisotropy < request.max_sampler_anisotropy) {
            LOG_WARN("Sampler anisotropy {} requested; clamped to device limit {}",
                request.max_sampler_anisotropy,
                capability.max_sampler_anisotropy);
        } else {
            LOG_INFO("Enabled optional device feature: samplerAnisotropy (max: {})",
                capability.max_sampler_anisotropy);
        }

        return capability;
    }
}
