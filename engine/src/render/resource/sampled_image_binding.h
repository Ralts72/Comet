#pragma once

#include "graphics/pipeline/descriptor_set.h"

namespace Comet {
    // Immutable descriptor with consecutive sampled-image bindings, retained by its frame.
    struct SampledImageBinding {
        static Result<std::shared_ptr<SampledImageBinding>, GraphicsError> create(Device& device,
            std::vector<std::shared_ptr<ImageView>> images,
            std::shared_ptr<DescriptorSetLayout> layout, std::shared_ptr<Sampler> sampler);

        std::vector<std::shared_ptr<ImageView>> images;
        std::shared_ptr<DescriptorSetLayout> layout;
        std::shared_ptr<Sampler> sampler;
        std::unique_ptr<DescriptorPool> pool;
        DescriptorSet descriptor;
    };
}
