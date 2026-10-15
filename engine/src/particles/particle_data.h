#pragma once

#include "common/export.h"
#include "asset/handle.h"
#include "common/result.h"
#include "core/math_utils.h"

#include <cstdint>
#include <vector>

namespace Comet {
    enum class ParticleBlend { Additive, Alpha };

    struct COMET_API ParticleEmitterComponent {
        static constexpr uint32_t MAX_CAPACITY = 4096;
        bool enabled = true;
        uint32_t capacity = 128;
        uint32_t seed = 1;
        uint32_t burst = 0;
        float rate = 20;
        float lifetime = 1;
        float speed = 2;
        float spread = 45;
        Math::Vec3 acceleration{0, -2, 0};
        Math::Vec4 start_color{1, 0.7f, 0.2f, 1};
        Math::Vec4 end_color{1, 0.2f, 0, 0};
        float start_size = 0.15f;
        float end_size = 0;
        AssetHandle texture;
        ParticleBlend blend = ParticleBlend::Additive;

        [[nodiscard]] Result<void> validate() const;
        bool operator==(const ParticleEmitterComponent&) const = default;
    };

    struct ParticleInstance {
        Math::Vec4 position_size{};
        Math::Vec4 color{};
        bool operator==(const ParticleInstance&) const = default;
    };

    // Main 发布的世界空间数据；Render 只上传，不访问实体或模拟状态。
    struct ParticleSnapshot {
        struct Batch {
            AssetHandle texture;
            uint32_t first;
            uint32_t count;
            ParticleBlend blend = ParticleBlend::Additive;
        };
        static constexpr uint32_t MAX_PARTICLES = 65536;
        std::vector<ParticleInstance> instances;
        std::vector<Batch> batches;
    };
}
