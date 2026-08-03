#pragma once
#include "triangle_vert.h"
#include "triangle_frag.h"
#include "cube_vert.h"
#include "cube_frag.h"
#include "core/math_utils.h"
#include "cube_texture_vert.h"
#include "cube_texture_frag.h"

namespace Comet {
    struct PushConstant {
        Math::Mat4 model;
    };

    struct ViewProjectMatrix {
        Math::Mat4 view;
        Math::Mat4 projection;
    };

}
