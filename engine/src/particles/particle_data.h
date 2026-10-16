#pragma once

#include "common/export.h"
#include "asset/handle.h"
#include "common/result.h"
#include "core/math_utils.h"

#include <cstdint>
#include <vector>

namespace Comet {
    enum class ParticleBlend { Additive, Alpha };
    enum class ParticleCollision { None, Bounce, Kill };

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
        float soft_distance = 0;
        uint32_t atlas_columns = 1;
        uint32_t atlas_rows = 1;
        uint32_t frame_count = 1;
        float frame_rate = 0;
        bool loop_animation = false;
        ParticleCollision collision = ParticleCollision::None;
        float collision_radius = 0.025f;
        float restitution = 0.5f;
        float friction = 0.1f;
        uint32_t collision_layers = UINT32_MAX;

        [[nodiscard]] Math::Vec4 texture_region(double age) const;
        [[nodiscard]] Result<void> validate() const;
        bool operator==(const ParticleEmitterComponent&) const = default;
    };

    struct COMET_API TrailComponent {
        bool enabled = true;
        uint32_t capacity = 128;
        float lifetime = 0.5f;
        float minimum_distance = 0.02f;
        float break_distance = 5;
        float width = 0.15f;
        Math::Vec4 color{1, 0.5f, 0.1f, 1};
        AssetHandle texture;
        ParticleBlend blend = ParticleBlend::Additive;
        float soft_distance = 0;

        [[nodiscard]] Result<void> validate() const;
        bool operator==(const TrailComponent&) const = default;
    };

    struct ParticleInstance {
        Math::Vec4 position_size{};
        Math::Vec4 color{};
        Math::Vec4 uv_rect{0, 0, 1, 1};
        // w < 0 为 billboard；否则连接两个世界空间端点，w 为末端宽度。
        Math::Vec4 end_position_size{0, 0, 0, -1};
        bool operator==(const ParticleInstance&) const = default;
    };

    // Main 发布的世界空间数据；Render 只上传，不访问实体或模拟状态。
    struct COMET_API ParticleSnapshot {
        struct Batch {
            AssetHandle texture;
            uint32_t first;
            uint32_t count;
            ParticleBlend blend = ParticleBlend::Additive;
            float soft_distance = 0;
        };
        static constexpr uint32_t MAX_PARTICLES = 65536;
        std::vector<ParticleInstance> instances;
        std::vector<Batch> batches;
        [[nodiscard]] bool uses_soft_depth() const;
    };
}
