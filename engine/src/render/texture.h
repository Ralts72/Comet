#pragma once
#include "common/export.h"
#include "graphics/vk_common.h"
#include "graphics/convert.h"
#include "core/math_utils.h"

#include <cstdint>
#include <vector>

namespace Comet {
    class Image;
    class ImageView;
    class Buffer;
    class Device;

    struct TextureData {
        int width = 0;
        int height = 0;
        int channels = 0;
        Format format = Format::R8G8B8A8_UNORM;
        std::vector<std::uint8_t> pixels;
    };

    class COMET_API Texture {
    public:
        Texture(Device& device, const TextureData& data);
        Texture(Device& device, int width, int height, Math::Vec4u color);
        ~Texture();

        [[nodiscard]] int get_width() const { return m_width; }
        [[nodiscard]] int get_height() const { return m_height; }
        [[nodiscard]] int get_channels() const { return m_channels; }
        [[nodiscard]] std::shared_ptr<Image> get_image() const;
        [[nodiscard]] std::shared_ptr<ImageView> get_image_view() const { return m_image_view; }
    private:
        void create_image(Device& device, size_t size, const void* data);

        int m_width;
        int m_height;
        int m_channels;
        Format m_format;
        std::shared_ptr<ImageView> m_image_view;
    };
}
