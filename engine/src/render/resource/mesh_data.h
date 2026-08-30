#pragma once

#include "core/geometry.h"
#include "core/math_utils.h"

#include <cmath>
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

    [[nodiscard]] inline std::optional<AxisAlignedBox>
    calculate_mesh_bounds(const MeshData& data) {
        if(data.vertices.empty()) {
            return std::nullopt;
        }

        AxisAlignedBox bounds = AxisAlignedBox::from_point(
            data.vertices.front().position);
        for(const MeshVertex& vertex: data.vertices) {
            const Math::Vec3 position = vertex.position;
            if(!std::isfinite(position.x)
               || !std::isfinite(position.y)
               || !std::isfinite(position.z)) {
                return std::nullopt;
            }
            bounds.include(position);
        }
        return bounds.is_valid()
            ? std::optional<AxisAlignedBox>(bounds)
            : std::nullopt;
    }
}
