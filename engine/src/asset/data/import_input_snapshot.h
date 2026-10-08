#pragma once

#include <cstdint>
#include <filesystem>
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
}
