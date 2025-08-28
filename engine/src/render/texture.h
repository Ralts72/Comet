#pragma once
#include "graphics/vk_common.h"
#include "graphics/convert.h"
#include "core/math_utils.h"

namespace Comet {
    class Image;
    class ImageView;
    class Buffer;
    class Device;

    class Texture {
    public:
        explicit Texture(Device* device, const std::string& img_path, Format format = Format::B8G8R8A8_UNORM);
        Texture(Device* device, int width, int height, Math::Vec4u color);
        ~Texture();

        [[nodiscard]] int get_width() const { return m_width; }
        [[nodiscard]] int get_height() const { return m_height; }
        [[nodiscard]] int get_channels() const { return m_channels; }
        [[nodiscard]] std::shared_ptr<Image> get_image() const { return m_image; }
        [[nodiscard]] std::shared_ptr<ImageView> get_image_view() const { return m_image_view; }
    private:
        void create_image(Device* device, size_t size, const void* data);

        int m_width;
        int m_height;
        int m_channels;
        Format m_format;
        std::shared_ptr<Image> m_image;
        std::shared_ptr<ImageView> m_image_view;
    };
}