#pragma once
#include "triangle_vert.h"
#include "triangle_frag.h"
#include "cube_vert.h"
#include "cube_frag.h"
#include "core/math_utils.h"

namespace Comet {
    struct PushConstant {
        Math::Mat4 matrix;
    };
}
