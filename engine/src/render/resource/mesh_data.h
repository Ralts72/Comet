#pragma once

#include "core/math_utils.h"

#include <cstdint>
#include <vector>

namespace Comet {
    struct MeshVertex {
        Math::Vec3 position{};
        Math::Vec2 texcoord{};
        Math::Vec3 normal{};
    };

    struct MeshData {
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
    };
}
