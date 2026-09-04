#pragma once

#include "asset/database.h"
#include "core/project_paths.h"

#include <filesystem>

namespace Comet::AssetSourceOperations {
    [[nodiscard]] AssetScanReport move(AssetDatabase& database, const ProjectPaths& paths,
        AssetHandle handle, const std::filesystem::path& destination);
}
