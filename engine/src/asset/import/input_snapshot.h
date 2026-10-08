#pragma once

#include "common/export.h"
#include "common/result.h"
#include "asset/data/import_input_snapshot.h"

#include <filesystem>
#include <span>

namespace Comet {
    [[nodiscard]] COMET_API Result<ImportInputSnapshot> capture_import_inputs(
        const std::filesystem::path& asset_root, const std::filesystem::path& source_path,
        std::span<const std::filesystem::path> source_dependencies);

    [[nodiscard]] COMET_API bool import_inputs_are_current(
        const std::filesystem::path& asset_root, const ImportInputSnapshot& snapshot);
}
