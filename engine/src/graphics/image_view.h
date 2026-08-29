#pragma once
#include "vk_common.h"
#include "common/export.h"

#include <memory>

namespace Comet {
    class Device;
    class Image;

    class COMET_API ImageView {
    public:
        ImageView(
            Device& device,
            std::shared_ptr<Image> image,
            Flags<ImageAspect> aspect);
        ~ImageView();

        ImageView(const ImageView&) = delete;
        ImageView& operator=(const ImageView&) = delete;
        ImageView(ImageView&&) noexcept = delete;
        ImageView& operator=(ImageView&&) noexcept = delete;

        [[nodiscard]] vk::ImageView get() const { return m_image_view;}

        [[nodiscard]] const std::shared_ptr<Image>& get_image() const {
            return m_image;
        }

    private:
        Device& m_device;
        std::shared_ptr<Image> m_image;
        vk::ImageView m_image_view;
    };
}
