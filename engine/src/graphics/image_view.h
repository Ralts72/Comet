#pragma once
#include "vk_common.h"
#include "common/export.h"

namespace Comet {
    class Device;
    class Image;

    class COMET_API ImageView {
    public:
        ImageView(Device* device, const Image& image, Flags<ImageAspect> aspect);
        ~ImageView();

        ImageView(const ImageView&) = delete;
        ImageView& operator=(const ImageView&) = delete;
        ImageView(ImageView&&) noexcept = delete;
        ImageView& operator=(ImageView&&) noexcept = delete;

        [[nodiscard]] vk::ImageView get() const { return m_image_view;}
    private:
        vk::ImageView m_image_view;
        Device* m_device;
    };
}
