#include "texture.h"
#include <stb_image.h>

#include "glm/gtx/io.hpp"
#include "graphics/device.h"
#include "graphics/command_context.h"
#include "graphics/image.h"
#include "graphics/image_view.h"
#include "graphics/buffer.h"

namespace Comet {
    Texture::Texture(Device* device, const std::string& img_path, const Format format) : m_width(0), m_height(0), m_channels(0), m_format(format) {
        uint8_t* data = stbi_load(img_path.c_str(), &m_width, &m_height, &m_channels, STBI_rgb_alpha);
        if(!data) {
            LOG_FATAL("Failed to load texture image from path: {}", img_path);
        }
        LOG_INFO("Loaded texture: {}x{}, channels: {}", m_width, m_height, m_channels);
        const size_t size = m_width * m_height * Graphics::format_size_in_bytes(m_format);
        create_image(device, size, data);
        stbi_image_free(data);
    }

    Texture::Texture(Device* device, int width, int height, Math::Vec4u color) : m_width(width), m_height(height), m_channels(4) {
        m_format = Format::B8G8R8A8_UNORM;
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

    Texture::~Texture() {
        m_image_view.reset();
        m_image.reset();
    }

    void Texture::create_image(Device* device, size_t size, const void* data) {
        if(!data || size == 0) {
            LOG_ERROR("Invalid data or size for texture creation: data={}, size={}",
                static_cast<const void*>(data), size);
            return;
        }

        m_image = Image::create(device, {
            m_format, Math::Vec3u(m_width, m_height, 1),
            Flags<ImageUsage>(ImageUsage::Sampled) | Flags<ImageUsage>(ImageUsage::CopyDst)
        }, SampleCount::Count1);
        m_image_view = std::make_shared<ImageView>(device, *m_image, Flags<ImageAspect>(ImageAspect::Color));

        auto stage_buffer = Buffer::create_cpu_buffer(device, Flags<BufferUsage>(BufferUsage::CopySrc), size, data);

        auto ctx = device->create_command_context();
        // 1. Transition image layout from UNDEFINED to TRANSFER_DST_OPTIMAL
        ctx->transition_image_layout(m_image->get(),
            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

        // 2. Copy buffer to image
        auto extent = Graphics::get_extent(m_image->get_info().extent.x, m_image->get_info().extent.y);
        ctx->copy_buffer_to_image(stage_buffer->get(), m_image->get(),
            vk::ImageLayout::eTransferDstOptimal, extent);

        // 3. Transition image layout from TRANSFER_DST_OPTIMAL to SHADER_READ_ONLY_OPTIMAL
        ctx->transition_image_layout(m_image->get(),
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

        // 提交并等待完成
        ctx->submit_and_wait();

        stage_buffer.reset();
    }
}
