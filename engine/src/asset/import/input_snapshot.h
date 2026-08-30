#pragma once

#include "common/export.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Comet {
    struct ImportInputFingerprint {
        std::filesystem::path relative_path;
        std::uint64_t size = 0;
        std::uint64_t hash = 0;

        bool operator==(const ImportInputFingerprint&) const noexcept = default;
    };

    struct ImportInputSnapshot {
        std::vector<ImportInputFingerprint> files;

        bool operator==(const ImportInputSnapshot&) const noexcept = default;
    };

    [[nodiscard]] COMET_API ImportInputSnapshot capture_import_inputs(
        const std::filesystem::path& asset_root,
        const std::filesystem::path& source_path,
        std::span<const std::filesystem::path> source_dependencies);

    [[nodiscard]] COMET_API bool import_inputs_are_current(
        const std::filesystem::path& asset_root,
        const ImportInputSnapshot& snapshot);
}
