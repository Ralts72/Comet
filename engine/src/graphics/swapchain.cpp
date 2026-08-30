#include "swapchain.h"

#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "context.h"
#include "core/window.h"
#include "device.h"
#include "graphics/resource/image.h"
#include "graphics/synchronization/semaphore.h"

#include <utility>

namespace Comet {
    Swapchain::Swapchain(
        const Window& window,
        Context& context,
        Device& device,
        const SwapchainRequest& request)
        : m_window(window),
          m_context(context),
          m_device(device),
          m_request(request) {
        PROFILE_SCOPE("Swapchain::Constructor");
        if(!recreate()) {
            LOG_FATAL("Cannot create the initial swapchain for a zero-sized framebuffer");
        }
    }

    Swapchain::~Swapchain() {
        if(m_swapchain) {
            m_device.get().destroySwapchainKHR(m_swapchain);
        }
    }

    bool Swapchain::recreate() {
        PROFILE_SCOPE("Swapchain::Recreate");
        const auto physical_device = m_context.get_physical_device();
        const auto surface = m_context.get_surface();
        const auto capabilities = physical_device.getSurfaceCapabilitiesKHR(surface);
        const auto surface_formats = physical_device.getSurfaceFormatsKHR(surface);
        const auto present_modes = physical_device.getSurfacePresentModesKHR(surface);
        const auto framebuffer_size = m_window.get_framebuffer_size();

        const auto [status, config, message] = select_swapchain(
            capabilities,
            surface_formats,
            present_modes,
            vk::Extent2D{framebuffer_size.x, framebuffer_size.y},
            m_request);
        if(status == SwapchainStatus::Deferred) {
            LOG_DEBUG("Swapchain recreation deferred: {}", message);
            return false;
        }
        if(status == SwapchainStatus::Unsupported) {
            LOG_FATAL("Cannot create Vulkan swapchain: {}", message);
        }
        if(!message.empty()) {
            LOG_WARN("Swapchain selection: {}", message);
        }
        m_device.wait_idle();

        vk::SharingMode image_sharing_mode;
        std::vector<uint32_t> queue_family_indices;
        if(m_context.is_same_queue_families()) {
            image_sharing_mode = vk::SharingMode::eExclusive;
        } else {
            image_sharing_mode = vk::SharingMode::eConcurrent;
            queue_family_indices.push_back(
                m_context.get_graphics_queue_family().queue_family_index.value());
            queue_family_indices.push_back(
                m_context.get_present_queue_family().queue_family_index.value());
        }

        const vk::SwapchainKHR old_swapchain = m_swapchain;
        vk::SwapchainCreateInfoKHR create_info{};
        create_info.surface = surface;
        create_info.minImageCount = config.image_count;
        create_info.imageFormat = config.surface_format.format;
        create_info.imageColorSpace = config.surface_format.colorSpace;
        create_info.imageExtent = config.extent;
        create_info.imageArrayLayers = config.image_layers;
        create_info.imageUsage = config.usage;
        create_info.imageSharingMode = image_sharing_mode;
        create_info.queueFamilyIndexCount = static_cast<uint32_t>(
            queue_family_indices.size());
        create_info.pQueueFamilyIndices = queue_family_indices.empty()
                                              ? nullptr
                                              : queue_family_indices.data();
        create_info.preTransform = config.transform;
        create_info.compositeAlpha = config.composite_alpha;
        create_info.presentMode = config.present_mode;
        create_info.clipped = config.clipped ? VK_TRUE : VK_FALSE;
        create_info.oldSwapchain = old_swapchain;

        m_swapchain = m_device.get().createSwapchainKHR(create_info);
        const auto images = m_device.get().getSwapchainImagesKHR(m_swapchain);
        m_images.clear();
        m_images.reserve(images.size());
        const ImageInfo image_info{
            .format = Graphics::vk_to_format(config.surface_format.format),
            .extent = Math::Vec3u(config.extent.width, config.extent.height, 1),
            .usage = Flags<ImageUsage>(ImageUsage::ColorAttachment)
        };
        for(const auto image: images) {
            m_images.emplace_back(Image::wrap(m_device, image, image_info));
        }
        m_config = config;
        m_current_index = static_cast<uint32_t>(-1);

        if(old_swapchain) {
            m_device.get().destroySwapchainKHR(old_swapchain);
        }

        LOG_INFO(
            "Vulkan swapchain created: images={}, extent={}x{}, format={}, color_space={}, present_mode={}, transform={}, composite_alpha={}, usage={}, layers={}, clipped={}",
            m_images.size(),
            config.extent.width,
            config.extent.height,
            vk::to_string(config.surface_format.format),
            vk::to_string(config.surface_format.colorSpace),
            vk::to_string(config.present_mode),
            vk::to_string(config.transform),
            vk::to_string(config.composite_alpha),
            vk::to_string(config.usage),
            config.image_layers,
            config.clipped);
        return true;
    }

    std::pair<uint32_t, vk::Result> Swapchain::acquire_next_image(
        const Semaphore& semaphore) {
        uint32_t image_index = 0;
        const auto result = m_device.get().acquireNextImageKHR(
            m_swapchain,
            UINT64_MAX,
            semaphore.get(),
            VK_NULL_HANDLE,
            &image_index);
        if(result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR) {
            m_current_index = image_index;
        }
        if(result == vk::Result::eSuccess
           || result == vk::Result::eSuboptimalKHR
           || result == vk::Result::eErrorOutOfDateKHR) {
            return std::make_pair(image_index, result);
        }
        LOG_FATAL("Failed to acquire swapchain image");
        return std::make_pair(image_index, result);
    }
}
