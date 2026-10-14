#pragma once

#include "asset/import/mesh_importer.h"

#include <fastgltf/types.hpp>

namespace Comet {
    inline constexpr std::size_t GLTF_IMPORT_OVERHEAD_BYTES = 16ull * 1024 * 1024;

    // 仅供 glTF 导入实现使用；第三方类型不进入公开资产接口。
    struct GltfMeshSelection {
        std::size_t mesh = 0;
        std::optional<std::size_t> primitive;
        std::optional<std::size_t> skin_node;
    };

    [[nodiscard]] Result<GltfMeshSelection> standalone_mesh_selection(const fastgltf::Asset& asset);
    [[nodiscard]] Result<std::size_t> estimate_gltf_mesh(const fastgltf::Asset& asset,
        const GltfMeshSelection& selection, std::size_t source_bytes,
        const AssetImportLimits& limits);
    [[nodiscard]] Result<MeshData> import_gltf_mesh(const fastgltf::Asset& asset,
        const GltfMeshSelection& selection, const std::filesystem::path& source_path);
}
