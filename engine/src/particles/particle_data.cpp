#include "particles/particle_data.h"

#include <cmath>
#include <algorithm>

namespace Comet {
    Result<void> ParticleEmitterComponent::validate() const {
        if(blend != ParticleBlend::Additive && blend != ParticleBlend::Alpha)
            return Result<void>::failure("Invalid particle blend mode");
        if(capacity == 0 || capacity > MAX_CAPACITY || burst > MAX_CAPACITY || !std::isfinite(rate)
            || rate < 0 || rate > 10000 || !std::isfinite(lifetime) || lifetime < 0.01f
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
        for(const auto color : {start_color, end_color}) {
            if(color.x < 0 || color.y < 0 || color.z < 0 || color.w < 0 || color.w > 1)
                return Result<void>::failure(
                    "Particle colors require nonnegative RGB and alpha in [0, 1]");
        }
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
        return std::ranges::any_of(
            batches, [](const auto& batch) { return batch.count != 0 && batch.soft_distance > 0; });
    }
}
