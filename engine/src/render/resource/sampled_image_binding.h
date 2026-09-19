#pragma once

#include "graphics/pipeline/descriptor_set.h"

namespace Comet {
    // 采样图像绑定编号连续；发布后不再修改描述符，由使用它的在途帧保活。
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
