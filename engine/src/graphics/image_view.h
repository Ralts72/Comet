#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;
    class Image;

    class ImageView {
    public:
        ImageView(Device* device, const Image& image, vk::ImageAspectFlags aspect);
        ~ImageView();
        [[nodiscard]] vk::ImageView get_image_view() const { return m_image_view;}
    private:
        vk::ImageView m_image_view;
        Device* m_device;
    };
}
