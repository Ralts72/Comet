#include "image.h"
#include "device.h"
#include "core/logger/logger.h"

namespace Comet {
    std::shared_ptr<Image> Image::create_owned_image(Device* device, const ImageInfo& info, vk::SampleCountFlagBits sample_count) {
        return std::make_shared<OwnedImage>(device, info, sample_count);
    }

    std::shared_ptr<Image> Image::create_borrowed_image(Device* device, vk::Image image, const ImageInfo& info) {
        return std::make_shared<BorrowedImage>(device, image, info);
    }

    Image::Image(Device* device, const ImageInfo& info, const ImageType type): m_device(device), m_info(info), m_type(type) {}

    OwnedImage::OwnedImage(Device* device, const ImageInfo& info, const vk::SampleCountFlagBits sample_count): Image(device, info, ImageType::Owned) {
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
        m_image = device->get().createImage(create_info);

        const auto memory_required = device->get().getImageMemoryRequirements(m_image);
        vk::MemoryAllocateInfo allocate_info = {};
        allocate_info.allocationSize = memory_required.size;
        allocate_info.memoryTypeIndex = device->get_memory_index(vk::MemoryPropertyFlagBits::eDeviceLocal, memory_required.memoryTypeBits);
        m_memory = device->get().allocateMemory(allocate_info);
        m_device->get().bindImageMemory(m_image, m_memory, 0);
        LOG_INFO("Vulkan image created successfully with memory allocated");
    }

    OwnedImage::~OwnedImage() {
        m_device->get().destroyImage(m_image);
        m_device->get().freeMemory(m_memory);
    }

    BorrowedImage::BorrowedImage(Device* device, vk::Image image, const ImageInfo& info): Image(device, info, ImageType::Borrowed) {
        m_image = image;
    }
}
