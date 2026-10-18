#pragma once

#include "core/math_utils.h"

namespace Comet {
    struct PushConstant {
        Math::Mat4 model;
        Math::Vec4 vertex_bend{0};
    };

    struct ViewProjectMatrix {
        Math::Mat4 view;
        Math::Mat4 projection;
        [[nodiscard]] Math::Vec4 depth_parameters() const {
            return {projection[2][2], projection[3][2], projection[2][3], projection[3][3]};
        }
        [[nodiscard]] bool is_cut_from(const ViewProjectMatrix& previous) const {
            const auto a = Math::inverse(previous.view);
            const auto b = Math::inverse(view);
            return projection != previous.projection
                   || Math::distance(Math::Vec3(a[3]), Math::Vec3(b[3])) > 5
                   || Math::dot(Math::Vec3(a[2]), Math::Vec3(b[2])) < .75f;
        }
    };

}
