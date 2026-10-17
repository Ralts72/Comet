#pragma once

#include "common/export.h"
#include "asset/handle.h"
#include "common/result.h"
#include "core/math_utils.h"

#include <cstdint>
#include <array>
#include <vector>

namespace Comet {
    enum class ParticleBlend { Additive, Alpha };
    enum class ParticleCollision { None, Bounce, Kill };
    enum class ParticleSimulation { CPU, GPU };

    struct COMET_API ParticleEmitterComponent {
        static constexpr uint32_t MAX_CAPACITY = 4096;
        static constexpr uint32_t MAX_GPU_CAPACITY = 65536;
        ParticleSimulation simulation = ParticleSimulation::CPU;
        bool gpu_dynamics = false;
        float turbulence = 0;
        float turbulence_frequency = 1;
        bool gpu_world_collision = false;
        uint32_t mesh_collision_budget = 256; // 每粒子、每积分子步的 BVH 节点／三角形访问上限。
        // 向量场的范围与方向均为世界空间；强度只缩放加速度。
        AssetHandle vector_field;
        float vector_field_strength = 1;
        Math::Vec3 wind{0}, vortex_center{0}, vortex_axis{0, 1, 0};
        float vortex_strength = 0, vortex_radius = 5;
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

    // Main 只保存出生批次；位置、颜色、尺寸、序列帧在 GPU 求值，无逐粒子 CPU 状态。
    struct GpuParticleSpawn {
        Math::Mat4 pose{1};
        double time = 0;
        double spacing = 0;
        uint64_t first = 0;
        uint32_t count = 0;
    };
    struct ParticleMeshCollider {
        uint64_t entity_id = 0;
        AssetHandle mesh;
        Math::Mat4 model{1};
        uint32_t layers = 1;
    };
    struct ParticleWorldCollider {
        static constexpr uint32_t MAX_COLLIDERS = 64;
        Math::Mat4 world_to_local{1};
        Math::Vec4 extent_shape{
            .5f, .5f, .5f, 0}; // XYZ 半尺寸／球半径，W: 0 盒、1 球、2 胶囊（X 半径，Y 圆柱半高）。
        std::array<uint32_t, 4> layers{1, 0, 0, 0};
    };
    struct GpuParticleEmitter {
        ParticleEmitterComponent settings;
        std::vector<GpuParticleSpawn> spawns;
        double time = 0;
        Math::Vec3 position{0};
        uint32_t count = 0;
        uint64_t id = 0;
    };

    // Main 发布自有快照；Render 不访问实体或 Main 上的可变模拟状态。
    struct COMET_API ParticleSnapshot {
        struct Batch {
            AssetHandle texture;
            uint32_t first;
            uint32_t count;
            ParticleBlend blend = ParticleBlend::Additive;
            float soft_distance = 0;
        };
        static constexpr uint32_t MAX_PARTICLES = 65536;
        static constexpr uint32_t MAX_GPU_PARTICLES = 262144;
        std::vector<ParticleInstance> instances;
        std::vector<Batch> batches;
        std::vector<GpuParticleEmitter> gpu_emitters;
        [[nodiscard]] bool uses_soft_depth() const;
    };
}
