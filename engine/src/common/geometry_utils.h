#pragma once
#include "core/math_utils.h"

namespace Comet {

    struct Cube {
        std::vector<Math::Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    class GeometryUtils {
    public:
        static Cube create_cube(float left, float right, float top, float bottom, float near, float far, const Math::Mat4& transform = Math::Mat4(1.0f));
    };
}
