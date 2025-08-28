#include "image_view.h"
#include "device.h"
#include "image.h"
#include "common/logger.h"

namespace Comet {
    ImageView::ImageView(Device* device, const Image& image, const Flags<ImageAspect> aspect): m_device(device) {
        vk::ImageViewCreateInfo create_info{};
        create_info.image = image.get();
        create_info.viewType = vk::ImageViewType::e2D;
        create_info.format = Graphics::format_to_vk(image.get_info().format);
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
        m_image_view = device->get().createImageView(create_info);
        LOG_INFO("Vulkan image view created successfully");
    }

    ImageView::~ImageView() {
        m_device->get().destroyImageView(m_image_view);
    }
}
