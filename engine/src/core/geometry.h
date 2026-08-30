#pragma once

#include "core/math_utils.h"

#include <algorithm>
#include <cmath>

namespace Comet {
    struct AxisAlignedBox {
        Math::Vec3 minimum{};
        Math::Vec3 maximum{};

        [[nodiscard]] static AxisAlignedBox from_point(
            const Math::Vec3 point) {
            return {.minimum = point, .maximum = point};
        }

        void include(const Math::Vec3 point) {
            minimum = {
                std::min(minimum.x, point.x),
                std::min(minimum.y, point.y),
                std::min(minimum.z, point.z)
            };
            maximum = {
                std::max(maximum.x, point.x),
                std::max(maximum.y, point.y),
                std::max(maximum.z, point.z)
            };
        }

        [[nodiscard]] bool is_valid() const {
            return std::isfinite(minimum.x)
                && std::isfinite(minimum.y)
                && std::isfinite(minimum.z)
                && std::isfinite(maximum.x)
                && std::isfinite(maximum.y)
                && std::isfinite(maximum.z)
                && minimum.x <= maximum.x
                && minimum.y <= maximum.y
                && minimum.z <= maximum.z;
        }

        [[nodiscard]] Math::Vec3 center() const {
            return (minimum + maximum) * 0.5f;
        }

        [[nodiscard]] Math::Vec3 size() const {
            return maximum - minimum;
        }
    };
}
