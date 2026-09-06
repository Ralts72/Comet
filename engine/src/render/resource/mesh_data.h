#pragma once

#include "core/math_utils.h"
#include "core/geometry.h"

#include <cstdint>
#include <optional>
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

    [[nodiscard]] inline std::optional<BoundingBox> calculate_mesh_bounds(
        const MeshData& data) {
        if(data.vertices.empty()) {
            return std::nullopt;
        }

        BoundingBox bounds = BoundingBox::from_point(data.vertices.front().position);
        for(const MeshVertex& vertex : data.vertices) {
            const Math::Vec3 position = vertex.position;
            if(!Math::is_finite(position)) {
                return std::nullopt;
            }
            bounds.include(position);
        }
        return bounds;
    }
}
