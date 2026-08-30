#include "graphics/resource/image_view.h"
#include "graphics/device.h"
#include "graphics/resource/image.h"
#include "diagnostics/logger.h"

#include <utility>

namespace Comet {
    ImageView::ImageView(
        Device& device,
        std::shared_ptr<Image> image,
        const Flags<ImageAspect> aspect)
        : m_device(device), m_image(std::move(image)) {
        if(!m_image) {
            LOG_FATAL("ImageView requires a valid image");
        }

        vk::ImageViewCreateInfo create_info{};
        create_info.image = m_image->get();
        create_info.viewType = vk::ImageViewType::e2D;
        create_info.format = Graphics::format_to_vk(m_image->get_info().format);
        create_info.components= {
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity
        };
        vk::ImageSubresourceRange subresource_range = {};
        subresource_range.aspectMask = Graphics::image_aspect_to_vk(aspect);
        subresource_range.baseMipLevel = 0;
        subresource_range.levelCount = 1;
        subresource_range.baseArrayLayer = 0;
        subresource_range.layerCount = 1;
        create_info.subresourceRange = subresource_range;
        m_image_view = device.get().createImageView(create_info);
        LOG_INFO("Vulkan image view created successfully");
    }

    ImageView::~ImageView() {
        m_device.get().destroyImageView(m_image_view);
    }
}
