#include "render/resource/texture.h"
#include "asset/data/texture_data.h"
#include "graphics/command/upload_manager.h"
#include "graphics/device.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/synchronization/resource_state.h"

#include <limits>
#include <algorithm>
#include <bit>
#include <span>

namespace Comet {
    GpuResourceResult<std::shared_ptr<Texture>> Texture::try_create(Device& device,
        UploadManager& upload_manager, const TextureData& data, const bool within_budget) {
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
        const auto max_mip_levels = static_cast<uint32_t>(std::bit_width(std::max(width, height)));
        if(data.mip_levels == 0 || data.mip_levels > max_mip_levels
            || (data.cubemap && width != height) || (!data.cubemap && data.mip_levels != 1))
            LOG_FATAL("Invalid texture mip count or cubemap dimensions");
        const uint32_t layers = data.cubemap ? 6 : 1;
        size_t expected_size = 0;
        for(uint32_t mip = 0; mip < data.mip_levels; ++mip) {
            const auto level_size = std::max(width >> mip, size_t{1})
                                    * std::max(height >> mip, size_t{1}) * bytes_per_pixel;
            if(level_size > (std::numeric_limits<size_t>::max() - expected_size) / layers)
                LOG_FATAL("Texture mip data size exceeds size_t range");
            expected_size += level_size * layers;
        }
        if(data.pixels.size() != expected_size) {
            LOG_FATAL("Texture pixel data size {} does not match expected size {}",
                data.pixels.size(), expected_size);
        }

        const ImageSubresourceRange subresources{
            .aspects = Flags<ImageAspect>(ImageAspect::Color),
            .level_count = data.mip_levels,
            .layer_count = layers,
        };
        const auto initial_state = resolve_image_state(ResourceUsage::Undefined, subresources);
        const auto sampled_state = resolve_image_state(ResourceUsage::SampledRead, subresources,
            Flags<PipelineStage>(PipelineStage::FragmentShader));
        if(!initial_state || !sampled_state) {
            LOG_FATAL("Failed to resolve texture upload states");
        }

        const ImageInfo image_info{
            .format = data.format,
            .extent = Math::Vec3u(data.width, data.height, 1),
            .usage = Flags<ImageUsage>(ImageUsage::Sampled) | ImageUsage::CopyDst,
            .mip_levels = data.mip_levels,
            .array_layers = layers,
            .cubemap = data.cubemap,
        };
        auto image_attempt = Image::try_create(
            device, image_info, within_budget, SampleCount::Count1, "texture image");
        if(!image_attempt) {
            return GpuResourceResult<std::shared_ptr<Texture>>::failure(image_attempt.result());
        }
        auto image = std::move(image_attempt).value();

        auto view_attempt =
            ImageView::try_create(device, image, Flags<ImageAspect>(ImageAspect::Color));
        if(!view_attempt) {
            return GpuResourceResult<std::shared_ptr<Texture>>::failure(view_attempt.result());
        }
        auto image_view = std::move(view_attempt).value();

        auto upload_batch = upload_manager.begin_batch();
        auto upload_attempt = upload_batch.try_enqueue_upload(image,
            std::as_bytes(std::span(data.pixels)), *initial_state, *sampled_state, within_budget);
        if(!upload_attempt) {
            return GpuResourceResult<std::shared_ptr<Texture>>::failure(upload_attempt.result());
        }

        const auto completion = upload_batch.submit();
        if(!completion)
            return GpuResourceResult<std::shared_ptr<Texture>>::failure(completion.result());
        std::shared_ptr<Texture> texture(
            new Texture(data.width, data.height, std::move(image_view), completion.value()));
        return GpuResourceResult<std::shared_ptr<Texture>>::success(std::move(texture));
    }

    Texture::Texture(const int width, const int height, std::shared_ptr<ImageView> image_view,
        const GpuCompletionPoint ready_completion)
        : m_width(width), m_height(height), m_image_view(std::move(image_view)),
          m_ready_completion(ready_completion) {}
}
