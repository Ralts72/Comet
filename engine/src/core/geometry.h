#pragma once

#include "common/export.h"
#include "core/math_utils.h"

#include <limits>
#include <optional>

namespace Comet {
    // 输入点所在坐标系中的轴对齐包围盒。
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
        // origin + t * direction 的参数上限；仅当方向归一化时才表示距离。
        float max_parameter = std::numeric_limits<float>::max();

        [[nodiscard]] bool is_valid() const;
    };

    // 仿射变换后的世界轴对齐包围盒；不接受投影矩阵。
    [[nodiscard]] COMET_API std::optional<BoundingBox> transform_box(
        const BoundingBox& box, const Math::Mat4& transform);

    [[nodiscard]] COMET_API std::optional<float> intersect_ray_box(
        const Ray& ray, const BoundingBox& box);

    // NDC 深度为 [0, 1]；返回近／远裁剪面之间的归一化射线，允许 x/y 超出视口。
    [[nodiscard]] COMET_API std::optional<Ray> unproject_ray(
        const Math::Mat4& inverse_view_projection, Math::Vec2 ndc);
}
