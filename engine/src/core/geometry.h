#pragma once

#include "core/math_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

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

    struct Ray {
        Math::Vec3 origin{};
        Math::Vec3 direction{0.0f, 0.0f, -1.0f};

        [[nodiscard]] bool is_valid() const {
            return std::isfinite(origin.x)
                && std::isfinite(origin.y)
                && std::isfinite(origin.z)
                && std::isfinite(direction.x)
                && std::isfinite(direction.y)
                && std::isfinite(direction.z)
                && Math::length(direction) > 0.000001f;
        }
    };

    [[nodiscard]] inline std::optional<float> intersect_ray_box(
        const Ray& ray,
        const AxisAlignedBox& box) {
        if(!ray.is_valid() || !box.is_valid()) {
            return std::nullopt;
        }

        float minimum_distance = 0.0f;
        float maximum_distance = std::numeric_limits<float>::max();
        for(int axis = 0; axis < 3; ++axis) {
            const float origin = ray.origin[axis];
            const float direction = ray.direction[axis];
            const float minimum = box.minimum[axis];
            const float maximum = box.maximum[axis];
            if(std::abs(direction) <= 0.000001f) {
                if(origin < minimum || origin > maximum) {
                    return std::nullopt;
                }
                continue;
            }

            float first = (minimum - origin) / direction;
            float second = (maximum - origin) / direction;
            if(first > second) {
                std::swap(first, second);
            }
            minimum_distance = std::max(minimum_distance, first);
            maximum_distance = std::min(maximum_distance, second);
            if(minimum_distance > maximum_distance) {
                return std::nullopt;
            }
        }
        return minimum_distance;
    }
}
