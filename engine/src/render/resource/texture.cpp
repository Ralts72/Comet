#include "render/resource/texture.h"
#include "graphics/command/upload_manager.h"
#include "graphics/device.h"
#include "graphics/convert.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/synchronization/resource_state.h"

#include <limits>
#include <span>

namespace Comet {
    std::shared_ptr<Texture> Texture::create(
        Device& device, UploadManager& upload_manager, const TextureData& data) {
        auto attempt = try_create(device, upload_manager, data, false);
        if(!attempt) {
            LOG_FATAL("Failed to create texture: {}", vk::to_string(attempt.result()));
        }
        return std::move(attempt).value();
    }

    GpuResourceResult<std::shared_ptr<Texture>> Texture::try_create(Device& device,
        UploadManager& upload_manager, const TextureData& data,
        const bool within_budget) {
        if(data.width <= 0 || data.height <= 0 || data.pixels.empty()) {
            LOG_FATAL("Texture requires valid decoded pixel data");
        }
        const size_t width = static_cast<size_t>(data.width);
        const size_t height = static_cast<size_t>(data.height);
        const size_t bytes_per_pixel = Graphics::format_size_in_bytes(data.format);
        if(width > std::numeric_limits<size_t>::max() / height
            || width * height > std::numeric_limits<size_t>::max() / bytes_per_pixel) {
            LOG_FATAL("Texture pixel data size exceeds size_t range");
        }
        const size_t expected_size = width * height * bytes_per_pixel;
        if(data.pixels.size() != expected_size) {
            LOG_FATAL("Texture pixel data size {} does not match expected size {}",
                data.pixels.size(), expected_size);
        }

        const ImageSubresourceRange subresources{
            .aspects = Flags<ImageAspect>(ImageAspect::Color),
        };
        const auto initial_state =
            resolve_image_state(ResourceUsage::Undefined, subresources);
        const auto sampled_state = resolve_image_state(ResourceUsage::SampledRead,
            subresources, Flags<PipelineStage>(PipelineStage::FragmentShader));
        if(!initial_state || !sampled_state) {
            LOG_FATAL("Failed to resolve texture upload states");
        }

        const ImageInfo image_info{
            .format = data.format,
            .extent = Math::Vec3u(data.width, data.height, 1),
            .usage = Flags<ImageUsage>(ImageUsage::Sampled) | ImageUsage::CopyDst,
        };
        auto image_attempt = Image::try_create(
            device, image_info, within_budget, SampleCount::Count1, "texture image");
        if(!image_attempt) {
            return GpuResourceResult<std::shared_ptr<Texture>>::failure(
                image_attempt.result());
        }
        auto image = std::move(image_attempt).value();

        auto view_attempt =
            ImageView::try_create(device, image, Flags<ImageAspect>(ImageAspect::Color));
        if(!view_attempt) {
            return GpuResourceResult<std::shared_ptr<Texture>>::failure(
                view_attempt.result());
        }
        auto image_view = std::move(view_attempt).value();

        auto upload_batch = upload_manager.begin_batch();
        auto upload_attempt =
            upload_batch.try_enqueue_upload(image, std::as_bytes(std::span(data.pixels)),
                *initial_state, *sampled_state, within_budget);
        if(!upload_attempt) {
            return GpuResourceResult<std::shared_ptr<Texture>>::failure(
                upload_attempt.result());
        }

        const GpuCompletionPoint completion = upload_batch.submit();
        std::shared_ptr<Texture> texture(
            new Texture(data.width, data.height, std::move(image_view), completion));
        return GpuResourceResult<std::shared_ptr<Texture>>::success(std::move(texture));
    }

    Texture::Texture(const int width, const int height,
        std::shared_ptr<ImageView> image_view, const GpuCompletionPoint ready_completion)
        : m_width(width), m_height(height), m_image_view(std::move(image_view)),
          m_ready_completion(ready_completion) {}
}
