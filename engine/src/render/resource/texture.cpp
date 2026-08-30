#include "render/resource/texture.h"
#include <glm/gtx/io.hpp>
#include "graphics/device.h"
#include "graphics/command/command_context.h"
#include "graphics/convert.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/buffer.h"

namespace Comet {
    Texture::Texture(Device& device, const TextureData& data)
        : m_width(data.width),
          m_height(data.height),
          m_format(data.format) {
        if(m_width <= 0 || m_height <= 0 || data.pixels.empty()) {
            LOG_FATAL("Texture requires valid decoded pixel data");
        }
        const size_t expected_size = static_cast<size_t>(m_width)
            * static_cast<size_t>(m_height)
            * Graphics::format_size_in_bytes(m_format);
        if(data.pixels.size() != expected_size) {
            LOG_FATAL(
                "Texture pixel data size {} does not match expected size {}",
                data.pixels.size(), expected_size);
        }
        create_image(device, data.pixels.size(), data.pixels.data());
    }

    Texture::Texture(Device& device, const int width, const int height, const Math::Vec4u color)
        : m_width(width), m_height(height) {
        m_format = Format::R8G8B8A8_UNORM;
        const size_t size = sizeof(uint8_t) * 4 * m_width * m_height;
        std::vector<uint8_t> pixels(size);
        for(int y = 0; y < m_height; ++y) {
            for(int x = 0; x < m_width; ++x) {
                const size_t idx = 4 * (y * m_width + x);
                pixels[idx + 0] = color.x; // R
                pixels[idx + 1] = color.y; // G
                pixels[idx + 2] = color.z; // B
                pixels[idx + 3] = color.w; // A
            }
        }
        create_image(device, size, pixels.data());
    }

    Texture::~Texture() = default;

    std::shared_ptr<Image> Texture::get_image() const {
        return m_image_view ? m_image_view->get_image() : nullptr;
    }

    void Texture::create_image(Device& device, size_t size, const void* data) {
        if(!data || size == 0) {
            LOG_ERROR("Invalid data or size for texture creation: data={}, size={}",
                static_cast<const void*>(data), size);
            return;
        }

        auto image = Image::create(device, {
            .format = m_format, .extent = Math::Vec3u(m_width, m_height, 1),
            .usage = Flags<ImageUsage>(ImageUsage::Sampled) | Flags<ImageUsage>(ImageUsage::CopyDst)
        }, SampleCount::Count1, "texture image");
        m_image_view = std::make_shared<ImageView>(device, image, Flags<ImageAspect>(ImageAspect::Color));

        auto stage_buffer = Buffer::create_upload_buffer(
            device,
            Flags<BufferUsage>(BufferUsage::CopySrc),
            size,
            data,
            "texture upload buffer");

        const auto ctx = device.create_command_context();
        // 1. Transition image layout from UNDEFINED to TRANSFER_DST_OPTIMAL
        ctx->transition_image_layout(image->get(),
            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

        // 2. Copy buffer to image
        const auto extent = Graphics::get_extent(image->get_info().extent.x, image->get_info().extent.y);
        ctx->copy_buffer_to_image(stage_buffer->get(), image->get(),
            vk::ImageLayout::eTransferDstOptimal, extent);

        // 3. Transition image layout from TRANSFER_DST_OPTIMAL to SHADER_READ_ONLY_OPTIMAL
        ctx->transition_image_layout(image->get(),
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

        // 提交并等待完成
        ctx->submit_and_wait();

        stage_buffer.reset();
    }
}
