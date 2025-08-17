#include "image.h"
#include "device.h"
#include "core/logger/logger.h"

namespace Comet {
    Image::Image(Device* device, const ImageInfo& info, const vk::SampleCountFlagBits sample_count)
    :m_device(device), m_info(info), m_owner(ImageOwnership::Owned) {
        auto tiling = vk::ImageTiling::eLinear;
        if(is_depth_stencil_format(info.format) || sample_count > vk::SampleCountFlagBits::e1){
            tiling = vk::ImageTiling::eOptimal;
        }
        vk::ImageCreateInfo create_info = {};
        create_info.imageType = vk::ImageType::e2D;
        create_info.format = m_info.format;
        create_info.extent = m_info.extent;
        create_info.mipLevels = 1;
        create_info.arrayLayers = 1;
        create_info.samples = sample_count;
        create_info.tiling = tiling;
        create_info.usage = m_info.usage;
        create_info.sharingMode = vk::SharingMode::eExclusive;
        create_info.queueFamilyIndexCount = 0;
        create_info.pQueueFamilyIndices = nullptr;
        create_info.initialLayout = vk::ImageLayout::eUndefined;
        m_image = device->get_device().createImage(create_info);

        const auto memory_required = device->get_device().getImageMemoryRequirements(m_image);
        vk::MemoryAllocateInfo allocate_info = {};
        allocate_info.allocationSize = memory_required.size;
        allocate_info.memoryTypeIndex = device->get_memory_index(vk::MemoryPropertyFlagBits::eDeviceLocal, memory_required.memoryTypeBits);
        m_memory = device->get_device().allocateMemory(allocate_info);
        m_device->get_device().bindImageMemory(m_image, m_memory, 0);
        LOG_INFO("Vulkan image created successfully with memory allocated");
    }
    Image::Image(Device* device, const vk::Image image, const ImageInfo& info)
    :m_image(image), m_device(device), m_info(info), m_owner(ImageOwnership::Borrowed) {}

    Image::~Image() {
        if(m_owner == ImageOwnership::Owned) {
            m_device->get_device().destroyImage(m_image);
            m_device->get_device().freeMemory(m_memory);
        }
    }
}
