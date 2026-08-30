#pragma once

#include "asset/metadata.h"
#include "common/export.h"
#include "core/project_paths.h"

#include <cstddef>
#include <compare>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Comet {
    struct AssetRecord {
        AssetHandle handle;
        AssetType type = AssetType::Unknown;
        std::filesystem::path path;
        AssetImportSettings import_settings;

        auto operator<=>(const AssetRecord&) const noexcept = default;
    };

    struct AssetScanIssue {
        std::filesystem::path path;
        std::string message;
    };

    struct AssetScanReport {
        std::size_t indexed_assets = 0;
        std::size_t generated_metadata = 0;
        std::vector<AssetScanIssue> issues;

        [[nodiscard]] bool succeeded() const noexcept {
            return issues.empty();
        }
    };

    class COMET_API AssetDatabase final {
    public:
        explicit AssetDatabase(ProjectPaths paths);

        [[nodiscard]] AssetScanReport scan();

        void update_import_settings(
            AssetHandle handle,
            AssetImportSettings import_settings);

        [[nodiscard]] const AssetRecord* find(AssetHandle handle) const;
        [[nodiscard]] const AssetRecord* find(const std::filesystem::path& path) const;
        [[nodiscard]] std::vector<AssetRecord> get_assets() const;
        [[nodiscard]] std::size_t size() const noexcept;

    private:
        ProjectPaths m_paths;
        std::unordered_map<AssetHandle, AssetRecord> m_assets;
        std::unordered_map<std::filesystem::path, AssetHandle> m_handles_by_path;
    };
}
