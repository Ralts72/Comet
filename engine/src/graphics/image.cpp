#include "image.h"
#include "device.h"
#include "common/logger.h"

namespace Comet {
    std::shared_ptr<Image> Image::create(Device* device, const ImageInfo& info, SampleCount sample_count) {
        return std::make_shared<OwnedImage>(device, info, sample_count);
    }

    std::shared_ptr<Image> Image::wrap(Device* device, vk::Image image, const ImageInfo& info) {
        return std::make_shared<BorrowedImage>(device, image, info);
    }

    Image::Image(Device* device, const ImageInfo& info) : m_device(device), m_info(info) {}

    OwnedImage::OwnedImage(Device* device, const ImageInfo& info, const SampleCount sample_count) : Image(device, info) {
        auto tiling = VK_IMAGE_TILING_LINEAR;
        if(Graphics::is_depth_stencil_format(info.format) || sample_count > SampleCount::Count1) {
            tiling = VK_IMAGE_TILING_OPTIMAL;
        }
        VkImageCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        create_info.imageType = VK_IMAGE_TYPE_2D;
        create_info.format = static_cast<VkFormat>(Graphics::format_to_vk(m_info.format));
        create_info.extent = VkExtent3D{m_info.extent.x, m_info.extent.y, m_info.extent.z};
        create_info.mipLevels = 1;
        create_info.arrayLayers = 1;
        create_info.samples = static_cast<VkSampleCountFlagBits>(Graphics::sample_count_to_vk(sample_count));
        create_info.tiling = tiling;
        create_info.usage = static_cast<VkImageUsageFlags>(Graphics::image_usage_to_vk(m_info.usage));
        create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocation_info = {};
        allocation_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        VkImage image = VK_NULL_HANDLE;
        const VkResult result = vmaCreateImage(
            device->get_allocator(), &create_info, &allocation_info, &image, &m_allocation, nullptr);
        if(result != VK_SUCCESS) {
            LOG_FATAL("Failed to create VMA image: {}", vk::to_string(static_cast<vk::Result>(result)));
        }
        m_image = vk::Image(image);
        LOG_INFO("Vulkan image created successfully with VMA allocation");
    }

    OwnedImage::~OwnedImage() {
        if(m_image && m_allocation) {
            vmaDestroyImage(m_device->get_allocator(), static_cast<VkImage>(m_image), m_allocation);
        }
    }

    BorrowedImage::BorrowedImage(Device* device, const vk::Image image, const ImageInfo& info) : Image(device, info) {
        m_image = image;
    }
}
