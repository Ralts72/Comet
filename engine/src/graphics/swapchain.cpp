#include "swapchain.h"
#include "context.h"
#include "device.h"
#include "pch.h"
#include "common/logger.h"
#include "common/config.h"
#include "common/profiler.h"
#include "image.h"
#include "semaphore.h"

namespace Comet {
    Swapchain::Swapchain(Context* context, Device* device) : m_context(context), m_device(device) {
        PROFILE_SCOPE("Swapchain::Constructor");
        recreate();
    }

    Swapchain::~Swapchain() {
        m_device->get().destroySwapchainKHR(m_swapchain);
    }

    bool Swapchain::recreate() {
        PROFILE_SCOPE("Swapchain::Recreate");
        setup_surface_capabilities();

        const uint32_t min_count = m_surface_info.capabilities.minImageCount;
        const uint32_t max_count = m_surface_info.capabilities.maxImageCount;
        uint32_t image_count = Config::get<uint32_t>("vulkan.swapchain_image_count", 3);
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
        m_swapchain = m_device->get().createSwapchainKHR(create_info);
        const auto images = m_device->get().getSwapchainImagesKHR(m_swapchain);
        m_images.clear();
        ImageInfo image_info = {};
        image_info.format = Graphics::vk_to_format(m_surface_info.surface_format.format);
        image_info.extent = Math::Vec3u(m_surface_info.capabilities.currentExtent.width, m_surface_info.capabilities.currentExtent.height, 1);
        image_info.usage = Flags<ImageUsage>(ImageUsage::ColorAttachment);
        for(const auto& image: images) {
            m_images.emplace_back(*Image::wrap(m_device, image, image_info));
        }
        LOG_INFO("Vulkan swapchain created successfully with {} images", m_images.size());
        // 销毁旧的交换链
        if(old_swapchain) {
            m_device->get().destroySwapchainKHR(old_swapchain);
        }

        return true;
    }

    std::pair<uint32_t, vk::Result> Swapchain::acquire_next_image(const Semaphore& semaphore) {
        uint32_t image_index;
        const auto result = m_device->get().acquireNextImageKHR(m_swapchain, UINT64_MAX,
            semaphore.get(), VK_NULL_HANDLE, &image_index);
        if(result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR) {
            m_current_index = image_index;
            return std::make_pair(image_index, result);
        }
        LOG_FATAL("failed to acquire image index");
    }

    void Swapchain::setup_surface_capabilities() {
        // capabilities
        m_surface_info.capabilities = m_context->get_physical_device().getSurfaceCapabilitiesKHR(m_context->get_surface());

        // 从 Config 读取设置
        const auto desired_surface_format = static_cast<Format>(Config::get<int>("vulkan.surface_format", 50));
        const auto desired_color_space = static_cast<ImageColorSpace>(Config::get<int>("vulkan.color_space", 0));

        // 根据 enable_vsync 配置决定 present mode
        const bool enable_vsync = Config::get<bool>("render.enable_vsync", false);
        vk::PresentModeKHR desired_present_mode;
        if(enable_vsync) {
            // VSync 启用：使用 Fifo 模式（垂直同步，限制帧率）
            desired_present_mode = vk::PresentModeKHR::eFifo;
        } else {
            // VSync 禁用：优先使用配置的模式，如果没有配置则使用 Immediate
            desired_present_mode = static_cast<vk::PresentModeKHR>(Config::get<int>("vulkan.present_mode", 0));
        }

        // format
        const auto surface_formats = m_context->get_physical_device().getSurfaceFormatsKHR(m_context->get_surface());
        m_surface_info.surface_format = surface_formats[0];
        for(const auto& format: surface_formats) {
            if(format.format == Graphics::format_to_vk(desired_surface_format) && format.colorSpace == Graphics::image_color_space_to_vk(desired_color_space)) {
                m_surface_info.surface_format = format;
                break;
            }
        }

        // present mode
        const auto present_modes = m_context->get_physical_device().getSurfacePresentModesKHR(m_context->get_surface());
        m_surface_info.present_mode = present_modes[0];
        for(const auto& mode: present_modes) {
            if(mode == desired_present_mode) {
                m_surface_info.present_mode = mode;
                break;
            }
        }

        LOG_DEBUG("current extent : {} x {}", m_surface_info.capabilities.currentExtent.width, m_surface_info.capabilities.currentExtent.height);
        LOG_DEBUG("surface format : {}", vk::to_string(m_surface_info.surface_format.format));
        LOG_DEBUG("present mode   : {}", vk::to_string(m_surface_info.present_mode));
    }
}
