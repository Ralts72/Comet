#include "render/resource/sampled_image_binding.h"

namespace Comet {
    Result<std::shared_ptr<SampledImageBinding>, GraphicsError> SampledImageBinding::create(
        Device& device, std::shared_ptr<ImageView> image,
        std::shared_ptr<DescriptorSetLayout> layout, std::shared_ptr<Sampler> sampler) {
        using Creation = Result<std::shared_ptr<SampledImageBinding>, GraphicsError>;
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(DescriptorType::CombinedImageSampler, 1);
        auto pool = DescriptorPool::create(device, 1, sizes);
        if(!pool)
            return Creation::failure(pool.error());
        auto sets = pool.value()->allocate_descriptor_set(*layout, 1);
        if(!sets)
            return Creation::failure(sets.error());
        auto binding = std::make_shared<SampledImageBinding>(SampledImageBinding{std::move(image),
            std::move(layout), std::move(sampler), std::move(pool).value(), sets.value().front()});
        const DescriptorSet::ImageSamplerWrite write{0, *binding->image, *binding->sampler};
        binding->descriptor.update(device, {}, std::span(&write, 1));
        return Creation::success(std::move(binding));
    }
}
