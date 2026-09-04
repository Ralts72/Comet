#pragma once

#include "common/export.h"
#include "render/resource/mesh_data.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace Comet::MeshImportCache {
    struct COMET_API Entry {
        MeshData data;
        std::vector<std::filesystem::path> source_dependencies;
    };

    [[nodiscard]] COMET_API std::optional<Entry> load_if_current(
        const std::filesystem::path& cache_path, const std::filesystem::path& asset_root,
        const std::filesystem::path& source_path, std::uint32_t importer_version);

    COMET_API void store(const std::filesystem::path& cache_path,
        const std::filesystem::path& asset_root, const std::filesystem::path& source_path,
        std::span<const std::filesystem::path> source_dependencies,
        std::uint32_t importer_version, const MeshData& data);
}
