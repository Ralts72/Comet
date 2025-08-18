#pragma once
#include "core/math_utils.h"

namespace Comet {

    struct Cube {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    class GeometryUtils {
    public:
        static Cube create_cube(float left, float right, float top, float bottom, float near, float far, const Mat4& transform = Mat4(1.0f));
    };
}
