#include "swapchain.h"
#include "context.h"
#include "device.h"
#include "pch.h"
#include "core/logger/logger.h"
#include "queue.h"
#include "image.h"
#include "semaphore.h"
#include "fence.h"

namespace Comet {
    Swapchain::Swapchain(Context* context, Device* device) : m_context(context), m_device(device) {
        recreate();
    }

    Swapchain::~Swapchain() {
        m_device->get_device().destroySwapchainKHR(m_swapchain);
    }

    bool Swapchain::recreate() {
        setup_surface_capabilities();

        const uint32_t min_count = m_surface_info.capabilities.minImageCount;
        const uint32_t max_count = m_surface_info.capabilities.maxImageCount;
        uint32_t image_count = m_device->get_settings().swapchain_image_count;
        if(max_count > 0) {
            image_count = std::clamp(image_count, min_count, max_count);
        } else {
            image_count = std::max(image_count, min_count);
        }

        vk::SharingMode image_sharing_mode;
        uint32_t queue_family_index_count = 0;
        std::vector<uint32_t> queue_family_indices;
        if(m_context->is_same_queue_families()) {
            image_sharing_mode = vk::SharingMode::eExclusive;
        } else {
            image_sharing_mode = vk::SharingMode::eConcurrent;
            queue_family_index_count = 2;
            queue_family_indices.push_back(m_context->get_graphics_queue_family().queue_family_index.value());
            queue_family_indices.push_back(m_context->get_present_queue_family().queue_family_index.value());
        }

        vk::SwapchainKHR old_swapchain = m_swapchain;

        vk::SwapchainCreateInfoKHR create_info = {};
        create_info.surface = m_context->get_surface();
        create_info.minImageCount = image_count;
        create_info.imageFormat = m_surface_info.surface_format.format;
        create_info.imageColorSpace = m_surface_info.surface_format.colorSpace;
        create_info.imageExtent = m_surface_info.capabilities.currentExtent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        create_info.imageSharingMode = image_sharing_mode;
        create_info.queueFamilyIndexCount = queue_family_index_count;
        create_info.pQueueFamilyIndices = queue_family_indices.data();
        create_info.preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        create_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
        create_info.presentMode = m_surface_info.present_mode;
        create_info.clipped = VK_FALSE;
        create_info.oldSwapchain = old_swapchain;
        m_swapchain = m_device->get_device().createSwapchainKHR(create_info);
        const auto images = m_device->get_device().getSwapchainImagesKHR(m_swapchain);
        m_images.clear();
        ImageInfo image_info = {};
        image_info.format = m_surface_info.surface_format.format;
        image_info.extent = vk::Extent3D{m_surface_info.capabilities.currentExtent.width, m_surface_info.capabilities.currentExtent.height, 1};
        image_info.usage = vk::ImageUsageFlagBits::eColorAttachment;
        for(const auto& image: images) {
            m_images.emplace_back(m_device, image, image_info);
        }
        LOG_INFO("Vulkan swapchain created successfully with {} images", m_images.size());
        // 销毁旧的交换链
        if(old_swapchain) {
            m_device->get_device().destroySwapchainKHR(old_swapchain);
        }

        return true;
    }

    uint32_t Swapchain::acquire_next_image(const Semaphore& semaphore) {
        uint32_t image_index;
        const auto result = m_device->get_device().acquireNextImageKHR(m_swapchain, UINT64_MAX,
            semaphore.get_semaphore(), VK_NULL_HANDLE, &image_index);
        if(result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR) {
            m_current_index = image_index;
            return image_index;
        }
        LOG_FATAL("failed to acquire image index");
    }

    void Swapchain::present(uint32_t image_index, const std::span<const Semaphore> wait_semaphores) const {
        std::vector<vk::Semaphore> vk_wait_semaphores;
        for(const auto& wait_sem: wait_semaphores) {
            vk_wait_semaphores.emplace_back(wait_sem.get_semaphore());
        }
        vk::PresentInfoKHR present_info = {};
        present_info.waitSemaphoreCount = static_cast<uint32_t>(vk_wait_semaphores.size());
        present_info.pWaitSemaphores = vk_wait_semaphores.data();
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &m_swapchain;
        present_info.pImageIndices = &image_index;
        const auto queue = m_device->get_present_queue(0);
        const auto result = queue->get_queue().presentKHR(present_info);
        if(result != vk::Result::eSuccess) {
            LOG_ERROR("Failed to present swapchain image: {}", vk::to_string(result));
        } else {
            LOG_DEBUG("Presented swapchain image at index: {}", image_index);
        }
        queue->wait_idle();
    }

    void Swapchain::setup_surface_capabilities() {
        // capabilities
        m_surface_info.capabilities = m_context->get_physical_device().getSurfaceCapabilitiesKHR(m_context->get_surface());

        const auto& settings = m_device->get_settings();
        // format
        const auto surface_formats = m_context->get_physical_device().getSurfaceFormatsKHR(m_context->get_surface());
        m_surface_info.surface_format = surface_formats[0];
        for(const auto& format: surface_formats) {
            if(format.format == settings.surface_format && format.colorSpace == settings.color_space) {
                m_surface_info.surface_format = format;
                break;
            }
        }

        // present mode
        const auto present_modes = m_context->get_physical_device().getSurfacePresentModesKHR(m_context->get_surface());
        m_surface_info.present_mode = present_modes[0];
        for(const auto& mode: present_modes) {
            if(mode == settings.present_mode) {
                m_surface_info.present_mode = mode;
                break;
            }
        }

        LOG_DEBUG("current extent : {} x {}", m_surface_info.capabilities.currentExtent.width, m_surface_info.capabilities.currentExtent.height);
        LOG_DEBUG("surface format : {}", vk::to_string(m_surface_info.surface_format.format));
        LOG_DEBUG("present mode   : {}", vk::to_string(m_surface_info.present_mode));
    }
}
