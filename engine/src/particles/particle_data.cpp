#include "particles/particle_data.h"

#include <cmath>

namespace Comet {
    Result<void> ParticleEmitterComponent::validate() const {
        if(capacity == 0 || capacity > MAX_CAPACITY || burst > MAX_CAPACITY || !std::isfinite(rate)
            || rate < 0 || rate > 10000 || !std::isfinite(lifetime) || lifetime < 0.01f
            || lifetime > 60 || !std::isfinite(speed) || speed < 0 || speed > 1000
            || !std::isfinite(spread) || spread < 0 || spread > 180
            || !Math::is_finite(acceleration) || !Math::is_finite(start_color)
            || !Math::is_finite(end_color) || !std::isfinite(start_size) || start_size < 0
            || !std::isfinite(end_size) || end_size < 0)
            return Result<void>::failure("Invalid particle emitter settings");
        for(const auto color : {start_color, end_color}) {
            if(color.x < 0 || color.y < 0 || color.z < 0 || color.w < 0 || color.w > 1)
                return Result<void>::failure(
                    "Particle colors require nonnegative RGB and alpha in [0, 1]");
        }
        return Result<void>::success();
    }
}
