#include "geometry.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Comet {
    BoundingBox BoundingBox::from_point(const Math::Vec3 point) {
        return {.minimum = point, .maximum = point};
    }

    void BoundingBox::include(const Math::Vec3 point) {
        minimum = {
            std::min(minimum.x, point.x),
            std::min(minimum.y, point.y),
            std::min(minimum.z, point.z),
        };
        maximum = {
            std::max(maximum.x, point.x),
            std::max(maximum.y, point.y),
            std::max(maximum.z, point.z),
        };
    }

    bool BoundingBox::is_valid() const {
        return Math::is_finite(minimum) && Math::is_finite(maximum)
               && minimum.x <= maximum.x && minimum.y <= maximum.y
               && minimum.z <= maximum.z;
    }

    Math::Vec3 BoundingBox::center() const {
        return minimum * 0.5f + maximum * 0.5f;
    }

    Math::Vec3 BoundingBox::size() const {
        return maximum - minimum;
    }

    bool Ray::is_valid() const {
        return Math::is_finite(origin) && Math::is_finite(direction)
               && (direction.x != 0.0f || direction.y != 0.0f || direction.z != 0.0f)
               && std::isfinite(max_parameter) && max_parameter >= 0.0f;
    }

    std::optional<BoundingBox> transform_box(
        const BoundingBox& box, const Math::Mat4& transform) {
        if(!box.is_valid() || transform[0][3] != 0.0f || transform[1][3] != 0.0f
            || transform[2][3] != 0.0f || transform[3][3] != 1.0f) {
            return std::nullopt;
        }

        std::optional<BoundingBox> result;
        for(int corner_index = 0; corner_index < 8; ++corner_index) {
            Math::Vec3 corner = box.minimum;
            for(int axis = 0; axis < 3; ++axis) {
                if((corner_index & (1 << axis)) != 0) {
                    corner[axis] = box.maximum[axis];
                }
            }
            const Math::Vec3 point(transform * Math::Vec4(corner, 1.0f));
            if(!Math::is_finite(point)) {
                return std::nullopt;
            }
            if(result) {
                result->include(point);
            } else {
                result = BoundingBox::from_point(point);
            }
        }
        return result;
    }

    std::optional<float> intersect_ray_box(const Ray& ray, const BoundingBox& box) {
        if(!ray.is_valid() || !box.is_valid()) {
            return std::nullopt;
        }

        double minimum_parameter = 0.0;
        double maximum_parameter = ray.max_parameter;
        for(int axis = 0; axis < 3; ++axis) {
            const double origin = ray.origin[axis];
            const double direction = ray.direction[axis];
            const double minimum = box.minimum[axis];
            const double maximum = box.maximum[axis];
            if(direction == 0.0) {
                if(origin < minimum || origin > maximum) {
                    return std::nullopt;
                }
                continue;
            }

            double first = (minimum - origin) / direction;
            double second = (maximum - origin) / direction;
            if(first > second) {
                std::swap(first, second);
            }
            minimum_parameter = std::max(minimum_parameter, first);
            maximum_parameter = std::min(maximum_parameter, second);
            if(minimum_parameter > maximum_parameter) {
                return std::nullopt;
            }
        }
        return static_cast<float>(minimum_parameter);
    }
}
