#pragma once
#include "common/export.h"
#include "core/math_utils.h"
#include "render/resource/texture_data.h"

#include <memory>

namespace Comet {
    class Image;
    class ImageView;
    class Buffer;
    class Device;

    class COMET_API Texture {
    public:
        Texture(Device& device, const TextureData& data);
        Texture(Device& device, int width, int height, Math::Vec4u color);
        ~Texture();

        [[nodiscard]] int get_width() const { return m_width; }
        [[nodiscard]] int get_height() const { return m_height; }
        [[nodiscard]] std::shared_ptr<Image> get_image() const;
        [[nodiscard]] std::shared_ptr<ImageView> get_image_view() const { return m_image_view; }
    private:
        void create_image(Device& device, size_t size, const void* data);

        int m_width;
        int m_height;
        Format m_format;
        std::shared_ptr<ImageView> m_image_view;
    };
}
