#pragma once

#include "common/export.h"
#include "core/math_utils.h"

#include <limits>
#include <optional>

namespace Comet {
    // Axis-aligned bounds in the coordinate space of the supplied points.
    struct COMET_API BoundingBox {
        Math::Vec3 minimum{};
        Math::Vec3 maximum{};

        [[nodiscard]] static BoundingBox from_point(Math::Vec3 point);
        void include(Math::Vec3 point);
        [[nodiscard]] bool is_valid() const;
        [[nodiscard]] Math::Vec3 center() const;
        [[nodiscard]] Math::Vec3 size() const;
    };

    struct COMET_API Ray {
        Math::Vec3 origin{};
        Math::Vec3 direction{0.0f, 0.0f, -1.0f};
        // Parameter limit in origin + t * direction; distance only for unit directions.
        float max_parameter = std::numeric_limits<float>::max();

        [[nodiscard]] bool is_valid() const;
    };

    // Returns world-aligned bounds for an affine transform; not a projection matrix.
    [[nodiscard]] COMET_API std::optional<BoundingBox> transform_box(
        const BoundingBox& box, const Math::Mat4& transform);

    [[nodiscard]] COMET_API std::optional<float> intersect_ray_box(
        const Ray& ray, const BoundingBox& box);
}
