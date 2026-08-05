#include "vk_capability.h"

#include "common/logger.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Comet {
    namespace {
        const std::vector<const char*> required_device_extensions = {
#ifdef __APPLE__
            "VK_KHR_portability_subset",
#endif
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        struct DeviceCandidate {
            DeviceCapability capability;
            uint32_t score = 0;
            std::vector<std::string> rejection_reasons;
            std::vector<std::string> notes;
            std::vector<std::string> score_reasons;
        };

        std::string join_strings(const std::vector<std::string>& values) {
            std::string result;
            for(std::size_t i = 0; i < values.size(); ++i) {
                if(i > 0) {
                    result += "; ";
                }
                result += values[i];
            }
            return result;
        }

        uint32_t get_device_type_score(const vk::PhysicalDeviceType type) {
            switch(type) {
                case vk::PhysicalDeviceType::eDiscreteGpu:
                    return 10000;
                case vk::PhysicalDeviceType::eIntegratedGpu:
                    return 5000;
                case vk::PhysicalDeviceType::eVirtualGpu:
                    return 2000;
                case vk::PhysicalDeviceType::eCpu:
                    return 1000;
                default:
                    return 500;
            }
        }

        bool supports_image_format(
            const vk::PhysicalDevice physical_device,
            const vk::Format format,
            const VkImageUsageFlags usage,
            const vk::SampleCountFlagBits sample_count) {
            VkImageFormatProperties properties{};
            const VkResult result = vkGetPhysicalDeviceImageFormatProperties(
                static_cast<VkPhysicalDevice>(physical_device),
                static_cast<VkFormat>(format),
                VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL,
                usage,
                0,
                &properties);
            return result == VK_SUCCESS
                && (properties.sampleCounts
                    & static_cast<VkSampleCountFlagBits>(sample_count)) != 0;
        }

        void select_queue_families(
            DeviceCandidate& candidate,
            const vk::SurfaceKHR surface,
            const uint32_t required_graphics_queue_count,
            const uint32_t required_present_queue_count) {
            const auto physical_device = candidate.capability.physical_device;
            const auto queue_families = physical_device.getQueueFamilyProperties();

            for(uint32_t index = 0; index < queue_families.size(); ++index) {
                const auto& family = queue_families[index];
                const bool supports_graphics = family.queueCount >= required_graphics_queue_count
                    && static_cast<bool>(family.queueFlags & vk::QueueFlagBits::eGraphics);
                const bool supports_present = family.queueCount >= required_present_queue_count
                    && physical_device.getSurfaceSupportKHR(index, surface);
                if(supports_graphics && supports_present) {
                    candidate.capability.graphics_queue_family = {index, family.queueCount};
                    candidate.capability.present_queue_family = {index, family.queueCount};
                    return;
                }
            }

            for(uint32_t index = 0; index < queue_families.size(); ++index) {
                const auto& family = queue_families[index];
                if(!candidate.capability.graphics_queue_family.queue_family_index
                    && family.queueCount >= required_graphics_queue_count
                    && static_cast<bool>(family.queueFlags & vk::QueueFlagBits::eGraphics)) {
                    candidate.capability.graphics_queue_family = {index, family.queueCount};
                }
                if(!candidate.capability.present_queue_family.queue_family_index
                    && family.queueCount >= required_present_queue_count
                    && physical_device.getSurfaceSupportKHR(index, surface)) {
                    candidate.capability.present_queue_family = {index, family.queueCount};
                }
            }

            if(!candidate.capability.graphics_queue_family.queue_family_index) {
                candidate.rejection_reasons.emplace_back("no graphics queue family with enough queues");
            }
            if(!candidate.capability.present_queue_family.queue_family_index) {
                candidate.rejection_reasons.emplace_back("no present queue family with enough queues");
            }
        }

        DeviceCandidate evaluate_device(
            const vk::PhysicalDevice physical_device,
            const vk::SurfaceKHR surface,
            const DeviceCapabilityRequest& request,
            const uint32_t required_graphics_queue_count,
            const uint32_t required_present_queue_count) {
            DeviceCandidate candidate;
            candidate.capability.physical_device = physical_device;

            const auto properties = physical_device.getProperties();
            select_queue_families(
                candidate,
                surface,
                required_graphics_queue_count,
                required_present_queue_count);

            std::set<std::string> available_extensions;
            for(const auto& extension : physical_device.enumerateDeviceExtensionProperties()) {
                available_extensions.emplace(extension.extensionName);
            }
            for(const char* extension : required_device_extensions) {
                if(!available_extensions.contains(extension)) {
                    candidate.rejection_reasons.emplace_back(
                        "missing required device extension " + std::string(extension));
                }
            }
            candidate.capability.enabled_extensions = required_device_extensions;

            const auto surface_capabilities = physical_device.getSurfaceCapabilitiesKHR(surface);
            if(!static_cast<bool>(surface_capabilities.supportedUsageFlags
                & vk::ImageUsageFlagBits::eColorAttachment)) {
                candidate.rejection_reasons.emplace_back(
                    "surface does not support color attachment images");
            }

            const auto surface_formats = physical_device.getSurfaceFormatsKHR(surface);
            if(surface_formats.empty()) {
                candidate.rejection_reasons.emplace_back("surface exposes no formats");
            } else {
                const bool supports_surface_format = std::ranges::any_of(
                    surface_formats,
                    [&request](const vk::SurfaceFormatKHR& format) {
                        return format.format == request.surface_format.format
                            && format.colorSpace == request.surface_format.colorSpace;
                    });
                if(!supports_surface_format) {
                    candidate.rejection_reasons.emplace_back(
                        "configured surface format/color space is unsupported");
                }
            }

            const auto present_modes = physical_device.getSurfacePresentModesKHR(surface);
            if(present_modes.empty()) {
                candidate.rejection_reasons.emplace_back("surface exposes no present modes");
            } else if(std::ranges::find(present_modes, request.present_mode) != present_modes.end()) {
                candidate.score += 100;
                candidate.score_reasons.emplace_back("configured present mode supported (+100)");
            } else {
                candidate.notes.emplace_back(
                    "configured present mode is unsupported; swapchain will use an available fallback");
            }

            if(!supports_image_format(
                physical_device,
                request.surface_format.format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                request.sample_count)) {
                candidate.rejection_reasons.emplace_back(
                    "configured color format does not support the requested MSAA sample count");
            }
            if(!supports_image_format(
                physical_device,
                request.depth_format,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                request.sample_count)) {
                candidate.rejection_reasons.emplace_back(
                    "configured depth format does not support the requested MSAA sample count");
            }

            const auto supported_features = physical_device.getFeatures();
            if(request.max_sampler_anisotropy > 1.0f) {
                if(supported_features.samplerAnisotropy) {
                    candidate.capability.enabled_features.samplerAnisotropy = VK_TRUE;
                    candidate.capability.max_sampler_anisotropy = std::min(
                        request.max_sampler_anisotropy,
                        properties.limits.maxSamplerAnisotropy);
                    candidate.score += 100;
                    candidate.score_reasons.emplace_back("sampler anisotropy supported (+100)");
                    if(candidate.capability.max_sampler_anisotropy
                        < request.max_sampler_anisotropy) {
                        candidate.notes.emplace_back(
                            "sampler anisotropy is clamped from "
                            + std::to_string(request.max_sampler_anisotropy) + " to "
                            + std::to_string(candidate.capability.max_sampler_anisotropy));
                    }
                } else {
                    candidate.notes.emplace_back(
                        "sampler anisotropy is unsupported and will fall back to 1");
                }
            }

            const uint32_t device_type_score = get_device_type_score(properties.deviceType);
            candidate.score += device_type_score;
            candidate.score_reasons.emplace_back(
                "device type " + vk::to_string(properties.deviceType) + " (+"
                + std::to_string(device_type_score) + ")");

            const uint32_t image_dimension_score = std::min(
                properties.limits.maxImageDimension2D / 16, 1000u);
            candidate.score += image_dimension_score;
            candidate.score_reasons.emplace_back(
                "max 2D image dimension (+" + std::to_string(image_dimension_score) + ")");
            if(candidate.capability.graphics_queue_family.queue_family_index
                == candidate.capability.present_queue_family.queue_family_index) {
                candidate.score += 500;
                candidate.score_reasons.emplace_back("shared graphics/present queue (+500)");
            }

            return candidate;
        }
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

    DeviceCapability select_physical_device(
        const std::vector<vk::PhysicalDevice>& physical_devices,
        const vk::SurfaceKHR surface,
        const DeviceCapabilityRequest& request,
        const uint32_t required_graphics_queue_count,
        const uint32_t required_present_queue_count) {
        if(physical_devices.empty()) {
            LOG_FATAL("No Vulkan physical devices found");
        }
        if(!surface) {
            LOG_FATAL("Physical device selection requires a valid Vulkan surface");
        }
        if(!std::isfinite(request.max_sampler_anisotropy)
            || request.max_sampler_anisotropy < 1.0f) {
            LOG_FATAL("Device sampler anisotropy request must be a finite number of at least 1.0");
        }

        const auto sample_count = static_cast<uint32_t>(request.sample_count);
        if(sample_count == 0 || (sample_count & (sample_count - 1)) != 0) {
            LOG_FATAL("Device MSAA sample count must contain exactly one valid sample-count bit");
        }

        std::optional<DeviceCandidate> selected_candidate;
        std::vector<std::string> rejected_devices;
        for(const auto physical_device : physical_devices) {
            DeviceCandidate candidate = evaluate_device(
                physical_device,
                surface,
                request,
                required_graphics_queue_count,
                required_present_queue_count);
            const std::string device_name = physical_device.getProperties().deviceName.data();

            if(!candidate.rejection_reasons.empty()) {
                const std::string reasons = join_strings(candidate.rejection_reasons);
                LOG_WARN("Rejected physical device '{}': {}", device_name, reasons);
                rejected_devices.emplace_back(device_name + ": " + reasons);
                continue;
            }

            LOG_INFO("Physical device candidate '{}' accepted with score {}: {}",
                device_name, candidate.score, join_strings(candidate.score_reasons));
            if(!selected_candidate || candidate.score > selected_candidate->score) {
                selected_candidate = std::move(candidate);
            }
        }

        if(!selected_candidate) {
            LOG_FATAL("No suitable Vulkan physical device found: {}", join_strings(rejected_devices));
        }

#ifdef BUILD_TYPE_DEBUG
        const auto selected_properties =
            selected_candidate->capability.physical_device.getProperties();
        LOG_INFO("Selected physical device '{}' with score {}",
            selected_properties.deviceName.data(), selected_candidate->score);
        LOG_INFO("Graphics queue family: {}, present queue family: {}",
            selected_candidate->capability.graphics_queue_family.queue_family_index.value(),
            selected_candidate->capability.present_queue_family.queue_family_index.value());
        for(const char* extension : selected_candidate->capability.enabled_extensions) {
            LOG_INFO("Enabled device extension: {}", extension);
        }
        for(const auto& note : selected_candidate->notes) {
            LOG_WARN("Physical device selection: {}", note);
        }
        if(request.max_sampler_anisotropy <= 1.0f) {
            LOG_INFO("Sampler anisotropy disabled by render configuration");
        } else if(selected_candidate->capability.enabled_features.samplerAnisotropy) {
            LOG_INFO("Enabled optional device feature: samplerAnisotropy (max: {})",
                selected_candidate->capability.max_sampler_anisotropy);
        }
#endif

        return std::move(selected_candidate->capability);
    }
}
