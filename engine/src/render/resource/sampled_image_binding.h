#pragma once

#include "graphics/pipeline/descriptor_set.h"

namespace Comet {
    // One immutable sampled-image descriptor, retained as a unit by its consuming frame.
    struct SampledImageBinding {
        static Result<std::shared_ptr<SampledImageBinding>, GraphicsError> create(Device& device,
            std::shared_ptr<ImageView> image, std::shared_ptr<DescriptorSetLayout> layout,
            std::shared_ptr<Sampler> sampler);

        std::shared_ptr<ImageView> image;
        std::shared_ptr<DescriptorSetLayout> layout;
        std::shared_ptr<Sampler> sampler;
        std::unique_ptr<DescriptorPool> pool;
        DescriptorSet descriptor;
    };
}
