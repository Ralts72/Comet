#pragma once

#include "common/export.h"
#include "render/resource/mesh_data.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace Comet::MeshImportCache {
    [[nodiscard]] COMET_API std::optional<MeshData> load_if_current(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& asset_root,
        const std::filesystem::path& source_path,
        std::uint32_t importer_version);

    COMET_API void store(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& asset_root,
        const std::filesystem::path& source_path,
        std::span<const std::filesystem::path> source_dependencies,
        std::uint32_t importer_version,
        const MeshData& data);
}
