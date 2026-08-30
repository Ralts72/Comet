#include "vk_capability.h"

#include "diagnostics/logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace Comet {
    namespace {
        const std::vector<const char*> required_device_extensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };
        const std::vector<const char*> optional_device_extensions = {
            VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
        };
        constexpr const char* portability_subset_extension =
            "VK_KHR_portability_subset";

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

        std::optional<vk::SurfaceFormatKHR> find_surface_format(
            const std::vector<vk::SurfaceFormatKHR>& formats,
            const vk::SurfaceFormatKHR requested) {
            const auto exact_match = std::ranges::find(formats, requested);
            if(exact_match != formats.end()) {
                return *exact_match;
            }
            if(formats.size() == 1
               && formats.front().format == vk::Format::eUndefined
               && formats.front().colorSpace == requested.colorSpace) {
                return requested;
            }
            return std::nullopt;
        }

        SwapchainResult unsupported_swapchain(std::string message) {
            return {
                .status = SwapchainStatus::Unsupported,
                .message = std::move(message)
            };
        }

        SwapchainResult deferred_swapchain(std::string message) {
            return {
                .status = SwapchainStatus::Deferred,
                .message = std::move(message)
            };
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

        std::string format_api_version(const uint32_t version) {
            return std::to_string(VK_API_VERSION_MAJOR(version)) + "."
                   + std::to_string(VK_API_VERSION_MINOR(version)) + "."
                   + std::to_string(VK_API_VERSION_PATCH(version));
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
                    candidate.capability.graphics_queue_family = {.queue_family_index = index, .queue_count = family.queueCount};
                    candidate.capability.present_queue_family = {.queue_family_index = index, .queue_count = family.queueCount};
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

            DeviceCandidateInfo candidate_info{
                .api_version = properties.apiVersion,
                .device_type = properties.deviceType,
                .max_image_dimension_2d = properties.limits.maxImageDimension2D,
                .has_graphics_queue =
                    candidate.capability.graphics_queue_family.queue_family_index.has_value(),
                .has_present_queue =
                    candidate.capability.present_queue_family.queue_family_index.has_value(),
                .shares_graphics_present_queue =
                    candidate.capability.graphics_queue_family.queue_family_index.has_value()
                    && candidate.capability.graphics_queue_family.queue_family_index
                       == candidate.capability.present_queue_family.queue_family_index
            };

            std::set<std::string> available_extensions;
            for(const auto& extension: physical_device.enumerateDeviceExtensionProperties()) {
                available_extensions.emplace(extension.extensionName);
            }
            auto extension_selection = select_device_extensions(
                available_extensions);
            candidate_info.missing_required_extensions = std::move(
                extension_selection.missing_required_extensions);
            candidate.capability.enabled_extensions = std::move(
                extension_selection.enabled_extensions);
            candidate.capability.memory_budget_enabled =
                extension_selection.memory_budget_enabled;
            candidate.capability.max_image_dimension_2d =
                properties.limits.maxImageDimension2D;

            const auto surface_capabilities = physical_device.getSurfaceCapabilitiesKHR(surface);
            const auto surface_formats = physical_device.getSurfaceFormatsKHR(surface);
            const auto present_modes = physical_device.getSurfacePresentModesKHR(surface);
            const vk::Extent2D probe_extent{
                std::max(surface_capabilities.minImageExtent.width, 1u),
                std::max(surface_capabilities.minImageExtent.height, 1u)
            };
            const auto swapchain_result = select_swapchain(
                surface_capabilities,
                surface_formats,
                present_modes,
                probe_extent,
                request.swapchain);
            candidate_info.swapchain_status = swapchain_result.status;
            candidate_info.swapchain_message = swapchain_result.message;
            candidate_info.requested_present_mode_supported = std::ranges::find(
                present_modes, request.swapchain.present_mode) != present_modes.end();

            candidate_info.color_format_supported = supports_image_format(
                physical_device,
                request.swapchain.surface_format.format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                request.sample_count);
            candidate_info.depth_format_supported = supports_image_format(
                physical_device,
                request.depth_format,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                request.sample_count);

            const auto supported_features = physical_device.getFeatures();
            candidate_info.sampler_anisotropy_supported = supported_features.samplerAnisotropy;
            candidate_info.max_sampler_anisotropy = properties.limits.maxSamplerAnisotropy;
            if(properties.apiVersion >= VK_API_VERSION_1_3) {
                vk::PhysicalDeviceVulkan12Features supported_vulkan12_features{};
                vk::PhysicalDeviceVulkan13Features supported_vulkan13_features{};
                vk::PhysicalDeviceFeatures2 supported_features2{};
                supported_features2.pNext = &supported_vulkan12_features;
                supported_vulkan12_features.pNext =
                    &supported_vulkan13_features;
                physical_device.getFeatures2(&supported_features2);
                candidate_info.timeline_semaphore_supported =
                    supported_vulkan12_features.timelineSemaphore;
                candidate_info.synchronization2_supported =
                    supported_vulkan13_features.synchronization2;
            }

            auto [score, rejection_reasons, notes, score_reasons,
                enabled_features, enabled_vulkan12_features,
                enabled_vulkan13_features, max_sampler_anisotropy] =
                evaluate_device_candidate(
                candidate_info, request);
            candidate.score = score;
            candidate.rejection_reasons = std::move(rejection_reasons);
            candidate.notes = std::move(notes);
            candidate.score_reasons = std::move(score_reasons);
            candidate.capability.enabled_features = enabled_features;
            candidate.capability.enabled_vulkan12_features =
                enabled_vulkan12_features;
            candidate.capability.enabled_vulkan13_features =
                enabled_vulkan13_features;
            candidate.capability.max_sampler_anisotropy = max_sampler_anisotropy;

            return candidate;
        }
    }

    DeviceExtensionSelection select_device_extensions(
        const std::set<std::string>& available_extensions) {
        DeviceExtensionSelection selection;
        for(const char* extension: required_device_extensions) {
            if(available_extensions.contains(extension)) {
                selection.enabled_extensions.push_back(extension);
            } else {
                selection.missing_required_extensions.emplace_back(extension);
            }
        }
        for(const char* extension: optional_device_extensions) {
            if(available_extensions.contains(extension)) {
                selection.enabled_extensions.push_back(extension);
            }
        }
        selection.memory_budget_enabled = available_extensions.contains(
            VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        if(available_extensions.contains(portability_subset_extension)) {
            selection.enabled_extensions.push_back(
                portability_subset_extension);
        }
        return selection;
    }

    DeviceCandidateEvaluation evaluate_device_candidate(
        const DeviceCandidateInfo& candidate,
        const DeviceCapabilityRequest& request) {
        DeviceCandidateEvaluation evaluation;

        if(candidate.api_version < request.required_api_version) {
            evaluation.rejection_reasons.emplace_back(
                "Vulkan API " + format_api_version(candidate.api_version)
                + " is below required " + format_api_version(request.required_api_version));
        }
        if(!candidate.has_graphics_queue) {
            evaluation.rejection_reasons.emplace_back(
                "no graphics queue family with enough queues");
        }
        if(!candidate.has_present_queue) {
            evaluation.rejection_reasons.emplace_back(
                "no present queue family with enough queues");
        }
        for(const auto& extension: candidate.missing_required_extensions) {
            evaluation.rejection_reasons.emplace_back(
                "missing required device extension " + extension);
        }
        if(candidate.swapchain_status != SwapchainStatus::Ready) {
            evaluation.rejection_reasons.emplace_back(
                "swapchain configuration failed: " + candidate.swapchain_message);
        } else if(candidate.requested_present_mode_supported) {
            evaluation.score += 100;
            evaluation.score_reasons.emplace_back(
                "configured present mode supported (+100)");
        } else if(!candidate.swapchain_message.empty()) {
            evaluation.notes.push_back(candidate.swapchain_message);
        }
        if(!candidate.color_format_supported) {
            evaluation.rejection_reasons.emplace_back(
                "configured color format does not support the requested MSAA sample count");
        }
        if(!candidate.depth_format_supported) {
            evaluation.rejection_reasons.emplace_back(
                "configured depth format does not support the requested MSAA sample count");
        }
        if(!candidate.synchronization2_supported) {
            evaluation.rejection_reasons.emplace_back(
                "required Vulkan 1.3 feature synchronization2 is unsupported");
        } else {
            evaluation.enabled_vulkan13_features.synchronization2 = VK_TRUE;
        }
        if(!candidate.timeline_semaphore_supported) {
            evaluation.rejection_reasons.emplace_back(
                "required Vulkan 1.2 feature timelineSemaphore is unsupported");
        } else {
            evaluation.enabled_vulkan12_features.timelineSemaphore = VK_TRUE;
        }

        if(request.max_sampler_anisotropy > 1.0f) {
            if(candidate.sampler_anisotropy_supported) {
                evaluation.enabled_features.samplerAnisotropy = VK_TRUE;
                evaluation.max_sampler_anisotropy = std::min(
                    request.max_sampler_anisotropy,
                    candidate.max_sampler_anisotropy);
                evaluation.score += 100;
                evaluation.score_reasons.emplace_back(
                    "sampler anisotropy supported (+100)");
                if(evaluation.max_sampler_anisotropy
                   < request.max_sampler_anisotropy) {
                    evaluation.notes.emplace_back(
                        "sampler anisotropy is clamped from "
                        + std::to_string(request.max_sampler_anisotropy) + " to "
                        + std::to_string(evaluation.max_sampler_anisotropy));
                }
            } else {
                evaluation.notes.emplace_back(
                    "sampler anisotropy is unsupported and will fall back to 1");
            }
        }

        const uint32_t device_type_score = get_device_type_score(candidate.device_type);
        evaluation.score += device_type_score;
        evaluation.score_reasons.emplace_back(
            "device type " + vk::to_string(candidate.device_type) + " (+"
            + std::to_string(device_type_score) + ")");

        const uint32_t image_dimension_score = std::min(
            candidate.max_image_dimension_2d / 16, 1000u);
        evaluation.score += image_dimension_score;
        evaluation.score_reasons.emplace_back(
            "max 2D image dimension (+" + std::to_string(image_dimension_score) + ")");
        if(candidate.shares_graphics_present_queue) {
            evaluation.score += 500;
            evaluation.score_reasons.emplace_back(
                "shared graphics/present queue (+500)");
        }

        return evaluation;
    }

    std::vector<const char*> get_available_names(
        const std::vector<const char*>& requested_names,
        const std::set<std::string>& available_names,
        const std::string_view item_type) {
        std::vector<const char*> selection;
        selection.reserve(requested_names.size());

        for(const char* name: requested_names) {
            if(available_names.contains(name)) {
                selection.push_back(name);
                LOG_INFO("Enabled {}: {}", item_type, name);
            } else {
                LOG_WARN("Requested {} not supported and skipped: {}", item_type, name);
            }
        }

        return selection;
    }

    SwapchainResult select_swapchain(
        const vk::SurfaceCapabilitiesKHR& capabilities,
        const std::vector<vk::SurfaceFormatKHR>& surface_formats,
        const std::vector<vk::PresentModeKHR>& present_modes,
        const vk::Extent2D framebuffer_extent,
        const SwapchainRequest& request) {
        if(framebuffer_extent.width == 0 || framebuffer_extent.height == 0) {
            return deferred_swapchain("framebuffer extent is zero");
        }
        if(capabilities.maxImageArrayLayers < 1) {
            return unsupported_swapchain("surface exposes no swapchain image layers");
        }
        if(capabilities.maxImageCount > 0
           && capabilities.maxImageCount < capabilities.minImageCount) {
            return unsupported_swapchain("surface image count limits are inconsistent");
        }
        if(capabilities.minImageExtent.width > capabilities.maxImageExtent.width
           || capabilities.minImageExtent.height > capabilities.maxImageExtent.height) {
            return unsupported_swapchain("surface extent limits are inconsistent");
        }
        if(!static_cast<bool>(request.usage & vk::ImageUsageFlagBits::eColorAttachment)) {
            return unsupported_swapchain("swapchain usage must include color attachment");
        }
        if((capabilities.supportedUsageFlags & request.usage) != request.usage) {
            return unsupported_swapchain("surface does not support the required swapchain usage");
        }
        if(!static_cast<bool>(capabilities.supportedTransforms
                              & capabilities.currentTransform)) {
            return unsupported_swapchain("surface current transform is not supported");
        }

        const auto surface_format = find_surface_format(
            surface_formats, request.surface_format);
        if(!surface_format) {
            return unsupported_swapchain(
                "configured surface format and color space are unavailable");
        }
        if(present_modes.empty()) {
            return unsupported_swapchain("surface exposes no present modes");
        }

        SwapchainConfig config;
        config.surface_format = *surface_format;
        config.usage = request.usage;
        config.transform = capabilities.currentTransform;

        if(capabilities.maxImageCount == 0) {
            config.image_count = std::max(request.image_count, capabilities.minImageCount);
        } else {
            config.image_count = std::clamp(
                request.image_count,
                capabilities.minImageCount,
                capabilities.maxImageCount);
        }
        if(config.image_count == 0) {
            return unsupported_swapchain("swapchain requires at least one image");
        }

        constexpr uint32_t variable_extent = std::numeric_limits<uint32_t>::max();
        if(capabilities.currentExtent.width != variable_extent
           && capabilities.currentExtent.height != variable_extent) {
            config.extent = capabilities.currentExtent;
        } else {
            config.extent.width = std::clamp(
                framebuffer_extent.width,
                capabilities.minImageExtent.width,
                capabilities.maxImageExtent.width);
            config.extent.height = std::clamp(
                framebuffer_extent.height,
                capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height);
        }
        if(config.extent.width == 0 || config.extent.height == 0) {
            return deferred_swapchain("selected swapchain extent is zero");
        }

        constexpr std::array alpha_preference = {
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
            vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
            vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
            vk::CompositeAlphaFlagBitsKHR::eInherit
        };
        const auto alpha = std::ranges::find_if(
            alpha_preference,
            [&capabilities](const vk::CompositeAlphaFlagBitsKHR candidate) {
                return static_cast<bool>(capabilities.supportedCompositeAlpha & candidate);
            });
        if(alpha == alpha_preference.end()) {
            return unsupported_swapchain("surface exposes no supported composite alpha mode");
        }
        config.composite_alpha = *alpha;

        SwapchainResult result{
            .status = SwapchainStatus::Ready,
            .config = config
        };
        if(std::ranges::find(present_modes, request.present_mode) != present_modes.end()) {
            result.config.present_mode = request.present_mode;
        } else {
            const auto fifo = std::ranges::find(
                present_modes, vk::PresentModeKHR::eFifo);
            result.config.present_mode = fifo != present_modes.end()
                                             ? vk::PresentModeKHR::eFifo
                                             : present_modes.front();
            result.message = "requested present mode is unavailable; using "
                             + vk::to_string(result.config.present_mode);
        }

        return result;
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
        for(const auto physical_device: physical_devices) {
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
                rejected_devices.emplace_back(device_name).append(": ").append(reasons);
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

#ifdef COMET_ENABLE_DEBUG_LOGS
        const auto selected_properties =
                selected_candidate->capability.physical_device.getProperties();
        LOG_INFO("Selected physical device '{}' with score {}",
            selected_properties.deviceName.data(), selected_candidate->score);
        LOG_INFO("Graphics queue family: {}, present queue family: {}",
            selected_candidate->capability.graphics_queue_family.queue_family_index.value(),
            selected_candidate->capability.present_queue_family.queue_family_index.value());
        for(const char* extension: selected_candidate->capability.enabled_extensions) {
            LOG_INFO("Enabled device extension: {}", extension);
        }
        for(const auto& note: selected_candidate->notes) {
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
