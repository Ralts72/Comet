#include "render/resource/sampled_image_binding.h"

namespace Comet {
    Result<std::shared_ptr<SampledImageBinding>, GraphicsError> SampledImageBinding::create(
        Device& device, std::vector<std::shared_ptr<ImageView>> images,
        std::shared_ptr<DescriptorSetLayout> layout, std::shared_ptr<Sampler> sampler) {
        using Creation = Result<std::shared_ptr<SampledImageBinding>, GraphicsError>;
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(
            DescriptorType::CombinedImageSampler, static_cast<uint32_t>(images.size()));
        auto pool = DescriptorPool::create(device, 1, sizes);
        if(!pool)
            return Creation::failure(pool.error());
        auto sets = pool.value()->allocate_descriptor_set(*layout, 1);
        if(!sets)
            return Creation::failure(sets.error());
        auto binding = std::make_shared<SampledImageBinding>(SampledImageBinding{std::move(images),
            std::move(layout), std::move(sampler), std::move(pool).value(), sets.value().front()});
        std::vector<DescriptorSet::ImageSamplerWrite> writes;
        for(uint32_t index = 0; index < binding->images.size(); ++index)
            writes.push_back({index, *binding->images[index], *binding->sampler});
        binding->descriptor.update(device, {}, writes);
        return Creation::success(std::move(binding));
    }
}
