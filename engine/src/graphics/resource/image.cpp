#include "graphics/resource/image.h"
#include "graphics/device.h"
#include "diagnostics/logger.h"

namespace Comet {
    namespace {
        void validate_image_info(const ImageInfo& info) {
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

        vk::ImageCreateInfo build_image_create_info(
            const ImageInfo& info,
            const SampleCount sample_count) {
            vk::ImageCreateInfo create_info{};
            create_info.imageType = vk::ImageType::e2D;
            create_info.format = Graphics::format_to_vk(info.format);
            create_info.extent = Graphics::get_extent(
                info.extent.x, info.extent.y, info.extent.z);
            create_info.mipLevels = 1;
            create_info.arrayLayers = 1;
            create_info.samples = Graphics::sample_count_to_vk(sample_count);
            create_info.tiling = vk::ImageTiling::eOptimal;
            create_info.usage = Graphics::image_usage_to_vk(info.usage);
            create_info.sharingMode = vk::SharingMode::eExclusive;
            create_info.initialLayout = vk::ImageLayout::eUndefined;
            return create_info;
        }
    }

    std::shared_ptr<Image> Image::create(
        Device& device, const ImageInfo& info,
        const SampleCount sample_count, const std::string_view debug_name) {
        auto attempt = try_create(
            device, info, false, sample_count, debug_name);
        if(!attempt) {
            LOG_FATAL("Failed to create image '{}': {}",
                debug_name,
                vk::to_string(attempt.result()));
        }
        return std::move(attempt).value();
    }

    GpuResourceResult<std::shared_ptr<Image>> Image::try_create(
        Device& device, const ImageInfo& info, const bool within_budget,
        const SampleCount sample_count, const std::string_view debug_name) {
        validate_image_info(info);
        auto allocation = device.get_allocator().try_create_image(
            build_image_create_info(info, sample_count),
            {
                .usage = AllocationUsage::Device,
                .within_budget = within_budget,
                .debug_name = debug_name.empty() ? "image" : debug_name
            });
        if(!allocation) {
            return GpuResourceResult<std::shared_ptr<Image>>::failure(
                allocation.result());
        }

        std::shared_ptr<Image> image(new OwnedImage(device,
            info, std::move(allocation).value()));
        return GpuResourceResult<std::shared_ptr<Image>>::success(
            std::move(image));
    }

    std::shared_ptr<Image> Image::wrap(Device& device, vk::Image image, const ImageInfo& info) {
        if(!image) {
            LOG_FATAL("BorrowedImage requires a valid image handle");
        }
        return std::make_shared<BorrowedImage>(device, image, info);
    }

    Image::Image(Device& device, const ImageInfo& info) : m_device(device), m_info(info) {
        validate_image_info(info);
    }

    OwnedImage::OwnedImage(Device& device, const ImageInfo& info,
                           Allocator::ImageAllocation allocation)
        : Image(device, info) {
        m_image = allocation.image;
        m_allocation = std::move(allocation.allocation);
        LOG_INFO("Vulkan image created successfully with VMA allocation");
    }

    OwnedImage::~OwnedImage() {
        if(m_image && m_allocation) {
            m_device.get_allocator().destroy_image(m_image, m_allocation);
        }
    }

    BorrowedImage::BorrowedImage(Device& device, const vk::Image image,
        const ImageInfo& info) : Image(device, info) {
        if(!image) {
            LOG_FATAL("BorrowedImage requires a valid image handle");
        }
        m_image = image;
    }
}
