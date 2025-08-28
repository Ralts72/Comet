#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;
    class Image;

    class ImageView {
    public:
        ImageView(Device* device, const Image& image, Flags<ImageAspect> aspect);
        ~ImageView();
        [[nodiscard]] vk::ImageView get() const { return m_image_view;}
    private:
        vk::ImageView m_image_view;
        Device* m_device;
    };
}
