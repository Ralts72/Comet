#include "render/resource/sampled_image_binding.h"
#include "graphics/device.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"

namespace Comet {
    namespace {
        Result<std::shared_ptr<SampledImageBinding>, GraphicsError> allocate_binding(Device& device,
            std::vector<std::shared_ptr<ImageView>> images,
            std::shared_ptr<DescriptorSetLayout> layout,
            std::vector<std::shared_ptr<Sampler>> samplers, const DescriptorPoolSizes& sizes) {
            using Creation = Result<std::shared_ptr<SampledImageBinding>, GraphicsError>;
            auto pool = DescriptorPool::create(device, 1, sizes);
            if(!pool)
                return Creation::failure(pool.error());
            auto sets = pool.value()->allocate_descriptor_set(*layout, 1);
            if(!sets)
                return Creation::failure(sets.error());
            auto binding = std::make_shared<SampledImageBinding>(
                SampledImageBinding{std::move(images), std::move(layout), std::move(samplers),
                    std::move(pool).value(), sets.value().front()});
            return Creation::success(std::move(binding));
        }
    }

    Result<std::shared_ptr<SampledImageBinding>, GraphicsError> SampledImageBinding::create(
        Device& device, std::vector<std::shared_ptr<ImageView>> images,
        std::shared_ptr<DescriptorSetLayout> layout, std::shared_ptr<Sampler> sampler) {
        std::vector<std::shared_ptr<Sampler>> samplers(images.size(), std::move(sampler));
        return create(device, std::move(images), std::move(layout), std::move(samplers));
    }

    Result<std::shared_ptr<SampledImageBinding>, GraphicsError> SampledImageBinding::create(
        Device& device, std::vector<std::shared_ptr<ImageView>> images,
        std::shared_ptr<DescriptorSetLayout> layout,
        std::vector<std::shared_ptr<Sampler>> samplers) {
        using Creation = Result<std::shared_ptr<SampledImageBinding>, GraphicsError>;
        if(images.size() != samplers.size())
            return Creation::failure({"Each sampled image requires one sampler"});
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(
            DescriptorType::CombinedImageSampler, static_cast<uint32_t>(images.size()));
        auto created = allocate_binding(
            device, std::move(images), std::move(layout), std::move(samplers), sizes);
        if(!created)
            return created;
        auto binding = std::move(created).value();
        std::vector<DescriptorSet::ImageSamplerWrite> writes;
        for(uint32_t index = 0; index < binding->images.size(); ++index)
            writes.push_back({index, *binding->images[index], *binding->samplers[index]});
        binding->descriptor.update(device, {}, writes);
        return Creation::success(std::move(binding));
    }
    Result<std::shared_ptr<SampledImageBinding>, GraphicsError> SampledImageBinding::
        create_separate(Device& device, std::vector<std::shared_ptr<ImageView>> images,
            std::shared_ptr<DescriptorSetLayout> layout,
            std::vector<std::shared_ptr<Sampler>> samplers) {
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(DescriptorType::SampledImage, static_cast<uint32_t>(images.size()));
        if(!samplers.empty())
            sizes.add_pool_size(DescriptorType::Sampler, static_cast<uint32_t>(samplers.size()));
        auto created = allocate_binding(
            device, std::move(images), std::move(layout), std::move(samplers), sizes);
        if(!created)
            return created;
        auto binding = std::move(created).value();
        const auto image_count = static_cast<uint32_t>(binding->images.size());
        const auto count = image_count + static_cast<uint32_t>(binding->samplers.size());
        std::vector<vk::DescriptorImageInfo> infos(count);
        std::vector<vk::WriteDescriptorSet> writes(count);
        for(uint32_t index = 0; index < count; ++index) {
            const bool image = index < image_count;
            if(image) {
                infos[index].imageView = binding->images[index]->get();
                infos[index].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            } else
                infos[index].sampler = binding->samplers[index - image_count]->get();
            writes[index].dstSet = binding->descriptor.get();
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1;
            writes[index].descriptorType =
                image ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eSampler;
            writes[index].pImageInfo = &infos[index];
        }
        device.get().updateDescriptorSets(writes, {});
        return Result<std::shared_ptr<SampledImageBinding>, GraphicsError>::success(
            std::move(binding));
    }

}
