#pragma once

#include "asset/database.h"
#include "asset/data/material_data.h"
#include "core/project_paths.h"

#include <filesystem>

namespace Comet::AssetSourceOperations {
    [[nodiscard]] AssetScanReport create_material(AssetDatabase& database,
        const ProjectPaths& paths, const std::filesystem::path& destination,
        const MaterialData& data);

    [[nodiscard]] AssetScanReport create_script(AssetDatabase& database, const ProjectPaths& paths,
        const std::filesystem::path& destination);

    [[nodiscard]] AssetScanReport move(AssetDatabase& database, const ProjectPaths& paths,
        AssetHandle handle, const std::filesystem::path& destination);

    [[nodiscard]] AssetScanReport remove_asset(
        AssetDatabase& database, const ProjectPaths& paths, AssetHandle handle);

    [[nodiscard]] AssetScanReport import_files(AssetDatabase& database, const ProjectPaths& paths,
        std::span<const std::filesystem::path> sources, const std::filesystem::path& directory);
}
