#include "graphics/resource/image_view.h"
#include "graphics/device.h"
#include "graphics/resource/image.h"
#include "diagnostics/logger.h"

#include <utility>

namespace Comet {
    std::shared_ptr<ImageView> ImageView::create(
        Device& device, std::shared_ptr<Image> image, const Flags<ImageAspect> aspect) {
        auto attempt = try_create(device, std::move(image), aspect);
        if(!attempt) {
            LOG_FATAL("Failed to create image view: {}", vk::to_string(attempt.result()));
        }
        return std::move(attempt).value();
    }

    GpuResourceResult<std::shared_ptr<ImageView>> ImageView::try_create(
        Device& device, std::shared_ptr<Image> image, const Flags<ImageAspect> aspect) {
        if(!image) {
            LOG_FATAL("ImageView requires a valid image");
        }
        if(!aspect) {
            LOG_FATAL("ImageView requires at least one image aspect");
        }

        vk::ImageViewCreateInfo create_info{};
        create_info.image = image->get();
        create_info.viewType = vk::ImageViewType::e2D;
        create_info.format = Graphics::format_to_vk(image->get_info().format);
        create_info.components = {vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity};
        vk::ImageSubresourceRange subresource_range = {};
        subresource_range.aspectMask = Graphics::image_aspect_to_vk(aspect);
        subresource_range.baseMipLevel = 0;
        subresource_range.levelCount = 1;
        subresource_range.baseArrayLayer = 0;
        subresource_range.layerCount = 1;
        create_info.subresourceRange = subresource_range;

        vk::ImageView image_view{};
        const vk::Result result =
            device.get().createImageView(&create_info, nullptr, &image_view);
        if(result != vk::Result::eSuccess) {
            return GpuResourceResult<std::shared_ptr<ImageView>>::failure(result);
        }

        std::shared_ptr<ImageView> view(
            new ImageView(device, std::move(image), image_view));
        LOG_INFO("Vulkan image view created successfully");
        return GpuResourceResult<std::shared_ptr<ImageView>>::success(std::move(view));
    }

    ImageView::ImageView(
        Device& device, std::shared_ptr<Image> image, const vk::ImageView image_view)
        : m_device(device), m_image(std::move(image)), m_image_view(image_view) {}

    ImageView::~ImageView() {
        if(m_image_view) {
            m_device.get().destroyImageView(m_image_view);
        }
    }
}
