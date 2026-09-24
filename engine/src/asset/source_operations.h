#pragma once

#include "asset/database.h"
#include "asset/data/material_data.h"
#include "common/export.h"
#include "core/project_paths.h"

#include <filesystem>
#include <span>

namespace Comet::AssetSourceOperations {
    [[nodiscard]] COMET_API AssetScanReport create_material(AssetDatabase& database,
        const ProjectPaths& paths, const std::filesystem::path& destination,
        const MaterialData& data);

    [[nodiscard]] COMET_API AssetScanReport create_script(AssetDatabase& database,
        const ProjectPaths& paths, const std::filesystem::path& destination);

    [[nodiscard]] COMET_API AssetScanReport move(AssetDatabase& database, const ProjectPaths& paths,
        AssetHandle handle, const std::filesystem::path& destination);

    [[nodiscard]] COMET_API AssetScanReport remove_asset(
        AssetDatabase& database, const ProjectPaths& paths, AssetHandle handle);

    [[nodiscard]] COMET_API AssetScanReport import_files(AssetDatabase& database,
        const ProjectPaths& paths, std::span<const std::filesystem::path> sources,
        const std::filesystem::path& directory);
}
