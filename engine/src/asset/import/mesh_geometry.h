#pragma once

#include "asset/data/mesh_data.h"
#include "common/result.h"

#include <span>

namespace Comet {
    struct MeshPrimitiveRange {
        std::size_t first_index;
        std::size_t index_count;
        bool generate_normals;
        bool generate_tangents;
    };

    // 导入专用：按面角生成几何属性，并在接缝处分裂顶点、同步复制蒙皮权重。
    [[nodiscard]] Result<void> complete_mesh_geometry(
        MeshData& mesh, std::span<const MeshPrimitiveRange> primitives);
}
