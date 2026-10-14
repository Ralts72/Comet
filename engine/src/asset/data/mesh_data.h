#pragma once

#include "core/math_utils.h"
#include "core/geometry.h"
#include "animation/animation_data.h"
#include <memory>
#include <array>

#include <cstdint>
#include <optional>
#include <vector>

namespace Comet {
    struct MeshVertex {
        Math::Vec3 position{};
        Math::Vec2 texcoord{};
        Math::Vec3 normal{};
        // xyz 为切线，w 为副切线方向；零值表示源模型未提供切线。
        Math::Vec4 tangent{};
    };

    struct SkinInfluences {
        std::array<uint32_t, 4> joints{};
        Math::Vec4 weights{0};
    };

    static_assert(sizeof(MeshVertex) == 12 * sizeof(float));
    static_assert(sizeof(SkinInfluences) == 8 * sizeof(uint32_t));

    struct MeshData {
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<SkinInfluences> skin;
        std::shared_ptr<const AnimationData> animation;
    };

    [[nodiscard]] inline std::optional<BoundingBox> calculate_mesh_bounds(const MeshData& data) {
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
