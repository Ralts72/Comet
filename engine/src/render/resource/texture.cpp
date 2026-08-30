#include "render/resource/texture.h"
#include "graphics/command/upload_manager.h"
#include "graphics/device.h"
#include "graphics/convert.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/synchronization/resource_state.h"

namespace Comet {
    Texture::Texture(
        Device& device,
        UploadManager& upload_manager,
        const TextureData& data)
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
        create_image(
            device,
            upload_manager,
            std::as_bytes(std::span(data.pixels)));
    }

    Texture::Texture(
        Device& device,
        UploadManager& upload_manager,
        const int width,
        const int height,
        const Math::Vec4u color)
        : m_width(width), m_height(height) {
        if(m_width <= 0 || m_height <= 0) {
            LOG_FATAL("Texture requires positive dimensions");
        }
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
        create_image(
            device,
            upload_manager,
            std::as_bytes(std::span(pixels)));
    }

    Texture::~Texture() = default;

    void Texture::create_image(
        Device& device,
        UploadManager& upload_manager,
        const std::span<const std::byte> data) {
        if(data.empty()) {
            LOG_FATAL("Texture requires non-empty upload data");
        }

        auto image = Image::create(device, {
            .format = m_format, .extent = Math::Vec3u(m_width, m_height, 1),
            .usage = Flags<ImageUsage>(ImageUsage::Sampled) | Flags<ImageUsage>(ImageUsage::CopyDst)
        }, SampleCount::Count1, "texture image");
        m_image_view = std::make_shared<ImageView>(device, image, Flags<ImageAspect>(ImageAspect::Color));

        const ImageSubresourceRange subresources{
            .aspects = Flags<ImageAspect>(ImageAspect::Color)
        };
        const auto initial_state = resolve_image_state(
            ResourceUsage::Undefined,
            subresources);
        const auto sampled_state = resolve_image_state(
            ResourceUsage::SampledRead,
            subresources,
            Flags<PipelineStage>(PipelineStage::FragmentShader));
        if(!initial_state || !sampled_state) {
            LOG_FATAL("Failed to resolve texture upload states");
        }

        upload_manager.enqueue_upload(
            image,
            data,
            *initial_state,
            *sampled_state);
        const auto completion = upload_manager.flush_batch();
        if(!completion) {
            LOG_FATAL("Texture upload did not produce a completion point");
        }
        m_ready_completion = *completion;
    }
}
