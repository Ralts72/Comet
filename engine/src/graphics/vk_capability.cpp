#include "vk_capability.h"

#include "common/logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
            for(const auto& extension: physical_device.enumerateDeviceExtensionProperties()) {
                available_extensions.emplace(extension.extensionName);
            }
            for(const char* extension: required_device_extensions) {
                if(!available_extensions.contains(extension)) {
                    candidate.rejection_reasons.emplace_back(
                        "missing required device extension " + std::string(extension));
                }
            }
            candidate.capability.enabled_extensions = required_device_extensions;

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
            if(swapchain_result.status != SwapchainStatus::Ready) {
                candidate.rejection_reasons.emplace_back(
                    "swapchain configuration failed: " + swapchain_result.message);
            } else if(std::ranges::find(
                          present_modes, request.swapchain.present_mode) != present_modes.end()) {
                candidate.score += 100;
                candidate.score_reasons.emplace_back("configured present mode supported (+100)");
            } else if(!swapchain_result.message.empty()) {
                candidate.notes.push_back(swapchain_result.message);
            }

            if(!supports_image_format(
                physical_device,
                request.swapchain.surface_format.format,
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

#ifdef BUILD_TYPE_DEBUG
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
