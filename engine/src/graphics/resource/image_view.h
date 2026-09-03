#pragma once
#include "common/export.h"
#include "graphics/resource/resource_result.h"
#include "graphics/vk_common.h"

#include <memory>

namespace Comet {
    class Device;
    class Image;

    class COMET_API ImageView {
    public:
        [[nodiscard]] static std::shared_ptr<ImageView> create(
            Device& device, std::shared_ptr<Image> image,
            Flags<ImageAspect> aspect);
        [[nodiscard]] static GpuResourceResult<std::shared_ptr<ImageView>>
        try_create(Device& device, std::shared_ptr<Image> image,
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
        ImageView(Device& device, std::shared_ptr<Image> image,
            vk::ImageView image_view);

        Device& m_device;
        std::shared_ptr<Image> m_image;
        vk::ImageView m_image_view = VK_NULL_HANDLE;
    };
}
