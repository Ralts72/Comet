#include "particles/particle_data.h"

#include <cmath>
#include <algorithm>

namespace Comet {
    Result<void> ParticleEmitterComponent::validate() const {
        if(blend != ParticleBlend::Additive && blend != ParticleBlend::Alpha)
            return Result<void>::failure("Invalid particle blend mode");
        if(simulation != ParticleSimulation::CPU && simulation != ParticleSimulation::GPU)
            return Result<void>::failure("无效的粒子模拟方式");
        const bool gpu = simulation == ParticleSimulation::GPU;
        const uint32_t limit = gpu ? MAX_GPU_CAPACITY : MAX_CAPACITY;
        if(gpu && !gpu_dynamics && collision != ParticleCollision::None)
            return Result<void>::failure("GPU 深度碰撞需要启用有状态模拟；Jolt 碰撞使用 CPU 模式");
        if(mesh_collision_budget < 16 || mesh_collision_budget > 2048)
            return Result<void>::failure("GPU mesh collision budget must be 16–2048");
        if(!std::isfinite(vector_field_strength) || std::abs(vector_field_strength) > 1000
            || (vector_field && (!gpu || !gpu_dynamics)))
            return Result<void>::failure("向量场需要 GPU 有状态模拟，强度须为 -1000～1000");
        if(!Math::is_finite(wind) || !Math::is_finite(vortex_center)
            || !Math::is_finite(vortex_axis) || glm::length(vortex_axis) < 1e-5f
            || !std::isfinite(vortex_strength) || std::abs(vortex_strength) > 1000
            || !std::isfinite(vortex_radius) || vortex_radius <= 0 || vortex_radius > 100000)
            return Result<void>::failure("Invalid GPU wind or vortex field");
        if(!std::isfinite(turbulence) || turbulence < 0 || turbulence > 1000
            || !std::isfinite(turbulence_frequency) || turbulence_frequency <= 0
            || turbulence_frequency > 100)
            return Result<void>::failure("GPU 扰动强度须为 0～1000，频率须为 (0,100]");
        if(capacity == 0 || capacity > limit || burst > limit || !std::isfinite(rate) || rate < 0
            || rate > (gpu ? 1000000 : 10000) || !std::isfinite(lifetime) || lifetime < 0.01f
            || lifetime > 60 || !std::isfinite(speed) || speed < 0 || speed > 1000
            || !std::isfinite(spread) || spread < 0 || spread > 180
            || !Math::is_finite(acceleration) || !Math::is_finite(start_color)
            || !Math::is_finite(end_color) || !std::isfinite(start_size) || start_size < 0
            || !std::isfinite(end_size) || end_size < 0)
            return Result<void>::failure("Invalid particle emitter settings");
        if(!std::isfinite(soft_distance) || soft_distance < 0 || soft_distance > 1000)
            return Result<void>::failure("交界淡出距离须为 0～1000 米");
        if(atlas_columns == 0 || atlas_columns > 64 || atlas_rows == 0 || atlas_rows > 64)
            return Result<void>::failure("图集行列数须为 1～64");
        if(frame_count == 0 || frame_count > atlas_columns * atlas_rows)
            return Result<void>::failure("有效帧数须介于 1 和图集格数之间");
        if(!std::isfinite(frame_rate) || frame_rate < 0 || frame_rate > 240)
            return Result<void>::failure("序列帧帧率须为 0～240");
        if(collision != ParticleCollision::None && collision != ParticleCollision::Bounce
            && collision != ParticleCollision::Kill)
            return Result<void>::failure("无效的粒子碰撞模式");
        if(!std::isfinite(collision_radius) || collision_radius < 0 || collision_radius > 10
            || !std::isfinite(restitution) || restitution < 0 || restitution > 1
            || !std::isfinite(friction) || friction < 0 || friction > 1)
            return Result<void>::failure("粒子碰撞半径须为 0～10 米，弹性与摩擦须为 0～1");
        for(const auto color : {start_color, end_color}) {
            if(color.x < 0 || color.y < 0 || color.z < 0 || color.w < 0 || color.w > 1)
                return Result<void>::failure(
                    "Particle colors require nonnegative RGB and alpha in [0, 1]");
        }
        return Result<void>::success();
    }

    Result<void> TrailComponent::validate() const {
        if(capacity < 2 || capacity > 512 || !std::isfinite(lifetime) || lifetime < 0.01f
            || lifetime > 60 || !std::isfinite(minimum_distance) || minimum_distance < 0.001f
            || !std::isfinite(break_distance) || break_distance <= minimum_distance
            || !std::isfinite(width) || width <= 0 || !Math::is_finite(color) || color.x < 0
            || color.y < 0 || color.z < 0 || color.w < 0 || color.w > 1
            || !std::isfinite(soft_distance) || soft_distance < 0 || soft_distance > 1000
            || (blend != ParticleBlend::Additive && blend != ParticleBlend::Alpha))
            return Result<void>::failure("无效的拖尾设置：检查容量、寿命、采样距离、宽度与颜色");
        return Result<void>::success();
    }
    Math::Vec4 ParticleEmitterComponent::texture_region(double age) const {
        if(atlas_columns == 1 && atlas_rows == 1)
            return {0, 0, 1, 1};
        const double position = frame_rate > 0 ? age * frame_rate : age / lifetime * frame_count;
        double frame_index = std::max(0.0, std::floor(position));
        if(loop_animation && frame_rate > 0)
            frame_index = std::fmod(frame_index, double(frame_count));
        else
            frame_index = std::min(frame_index, double(frame_count - 1));
        const auto frame = uint32_t(frame_index);
        return {float(frame % atlas_columns) / atlas_columns,
            float(frame / atlas_columns) / atlas_rows, 1.0f / atlas_columns, 1.0f / atlas_rows};
    }
    bool ParticleSnapshot::uses_soft_depth() const {
        return std::ranges::any_of(batches, [](const auto& batch) {
            return batch.count != 0 && batch.soft_distance > 0;
        }) || std::ranges::any_of(gpu_emitters, [](const auto& emitter) {
            return emitter.count != 0 && emitter.settings.soft_distance > 0;
        });
    }
}
