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
            return minimum * 0.5f + maximum * 0.5f;
        }

        [[nodiscard]] Math::Vec3 size() const {
            return maximum - minimum;
        }
    };

    struct Ray {
        Math::Vec3 origin{};
        Math::Vec3 direction{0.0f, 0.0f, -1.0f};

        [[nodiscard]] bool is_valid() const {
            const float direction_length = Math::length(direction);
            return std::isfinite(origin.x)
                && std::isfinite(origin.y)
                && std::isfinite(origin.z)
                && std::isfinite(direction.x)
                && std::isfinite(direction.y)
                && std::isfinite(direction.z)
                && std::isfinite(direction_length)
                && direction_length > 0.000001f;
        }
    };

    [[nodiscard]] inline std::optional<AxisAlignedBox> transform_box(
        const AxisAlignedBox& box,
        const Math::Mat4& transform) {
        if(!box.is_valid()) {
            return std::nullopt;
        }

        std::optional<AxisAlignedBox> transformed_box;
        for(int corner_index = 0; corner_index < 8; ++corner_index) {
            const Math::Vec3 corner(
                (corner_index & 1) != 0 ? box.maximum.x : box.minimum.x,
                (corner_index & 2) != 0 ? box.maximum.y : box.minimum.y,
                (corner_index & 4) != 0 ? box.maximum.z : box.minimum.z);
            const Math::Vec4 transformed = transform
                * Math::Vec4(corner, 1.0f);
            if(!std::isfinite(transformed.x)
               || !std::isfinite(transformed.y)
               || !std::isfinite(transformed.z)
               || !std::isfinite(transformed.w)
               || std::abs(transformed.w) <= 0.000001f) {
                return std::nullopt;
            }

            const Math::Vec3 point = Math::Vec3(transformed)
                / transformed.w;
            if(!std::isfinite(point.x)
               || !std::isfinite(point.y)
               || !std::isfinite(point.z)) {
                return std::nullopt;
            }
            if(transformed_box) {
                transformed_box->include(point);
            } else {
                transformed_box = AxisAlignedBox::from_point(point);
            }
        }
        return transformed_box;
    }

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
