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
    };

}
