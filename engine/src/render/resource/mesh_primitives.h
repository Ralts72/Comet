#pragma once

#include "common/export.h"
#include "render/resource/mesh_data.h"

namespace Comet::MeshPrimitives {
    [[nodiscard]] COMET_API MeshData create_cube(float left, float right, float top,
        float bottom, float near, float far,
        const Math::Mat4& transform = Math::Mat4(1.0f));
}
