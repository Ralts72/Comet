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
        return std::isfinite(minimum.x) && std::isfinite(minimum.y)
               && std::isfinite(minimum.z) && std::isfinite(maximum.x)
               && std::isfinite(maximum.y) && std::isfinite(maximum.z)
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
        return std::isfinite(origin.x) && std::isfinite(origin.y)
               && std::isfinite(origin.z) && std::isfinite(direction.x)
               && std::isfinite(direction.y) && std::isfinite(direction.z)
               && (direction.x != 0.0f || direction.y != 0.0f || direction.z != 0.0f)
               && std::isfinite(max_parameter) && max_parameter >= 0.0f;
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
