#include "image.h"
#include "device.h"
#include "diagnostics/logger.h"

namespace Comet {
    std::shared_ptr<Image> Image::create(Device& device,
                                         const ImageInfo& info,
                                         const SampleCount sample_count,
                                         const std::string_view debug_name) {
        return std::make_shared<OwnedImage>(device, info, sample_count, debug_name);
    }

    std::shared_ptr<Image> Image::wrap(Device& device, vk::Image image, const ImageInfo& info) {
        if(!image) {
            LOG_FATAL("BorrowedImage requires a valid image handle");
        }
        return std::make_shared<BorrowedImage>(device, image, info);
    }

    Image::Image(Device& device, const ImageInfo& info) : m_device(device), m_info(info) {
        if(info.extent.x == 0 || info.extent.y == 0 || info.extent.z == 0) {
            LOG_FATAL("Image extent must be greater than zero");
        }
        if(info.format == Format::UNDEFINED) {
            LOG_FATAL("Image format must be defined");
        }
        if(!info.usage) {
            LOG_FATAL("Image usage must not be empty");
        }
    }

    OwnedImage::OwnedImage(Device& device,
                           const ImageInfo& info,
                           const SampleCount sample_count,
                           const std::string_view debug_name)
        : Image(device, info) {
        const auto extent = Graphics::get_extent(m_info.extent.x, m_info.extent.y, m_info.extent.z);
        vk::ImageCreateInfo create_info = {};
        create_info.imageType = vk::ImageType::e2D;
        create_info.format = Graphics::format_to_vk(m_info.format);
        create_info.extent = extent;
        create_info.mipLevels = 1;
        create_info.arrayLayers = 1;
        create_info.samples = Graphics::sample_count_to_vk(sample_count);
        create_info.tiling = vk::ImageTiling::eOptimal;
        create_info.usage = Graphics::image_usage_to_vk(m_info.usage);
        create_info.sharingMode = vk::SharingMode::eExclusive;
        create_info.queueFamilyIndexCount = 0;
        create_info.pQueueFamilyIndices = nullptr;
        create_info.initialLayout = vk::ImageLayout::eUndefined;

        auto image_allocation = device.get_allocator().create_image(
            create_info,
            {
                .usage = AllocationUsage::Device,
                .debug_name = debug_name.empty() ? "image" : debug_name
            });
        m_image = image_allocation.image;
        m_allocation = std::move(image_allocation.allocation);
        LOG_INFO("Vulkan image created successfully with VMA allocation");
    }

    OwnedImage::~OwnedImage() {
        if(m_image && m_allocation) {
            m_device.get_allocator().destroy_image(m_image, m_allocation);
        }
    }

    BorrowedImage::BorrowedImage(Device& device, const vk::Image image, const ImageInfo& info) : Image(device, info) {
        if(!image) {
            LOG_FATAL("BorrowedImage requires a valid image handle");
        }
        m_image = image;
    }
}
