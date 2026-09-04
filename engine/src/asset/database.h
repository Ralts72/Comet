#pragma once

#include "asset/metadata.h"
#include "common/export.h"
#include "core/project_paths.h"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Comet {
    struct AssetRecord {
        AssetHandle handle;
        AssetType type = AssetType::Unknown;
        std::filesystem::path path;
        AssetImportSettings import_settings;
        std::vector<AssetHandle> dependencies;

        auto operator<=>(const AssetRecord&) const noexcept = default;
    };

    struct AssetScanIssue {
        std::filesystem::path path;
        std::string message;
    };

    struct AssetScanReport {
        std::size_t indexed_assets = 0;
        std::size_t generated_metadata = 0;
        bool snapshot_updated = false;
        std::vector<AssetHandle> added_assets;
        std::vector<AssetHandle> removed_assets;
        std::vector<AssetHandle> modified_assets;
        std::vector<AssetScanIssue> issues;

        [[nodiscard]] bool succeeded() const noexcept { return issues.empty(); }
    };

    class COMET_API AssetDatabase final {
    public:
        explicit AssetDatabase(ProjectPaths paths);

        [[nodiscard]] AssetScanReport scan();

        void update_import_settings(
            AssetHandle handle, AssetImportSettings import_settings);
        void update_dependencies(
            AssetHandle handle, std::vector<AssetHandle> dependencies);
        void update_import_dependencies(
            AssetHandle handle, std::vector<std::filesystem::path> dependencies);

        [[nodiscard]] const AssetRecord* find(AssetHandle handle) const;
        [[nodiscard]] const AssetRecord* find(const std::filesystem::path& path) const;
        [[nodiscard]] std::span<const AssetHandle> get_dependencies(
            AssetHandle handle) const;
        [[nodiscard]] std::span<const AssetHandle> get_dependents(
            AssetHandle handle) const;
        [[nodiscard]] std::span<const std::filesystem::path> get_import_dependencies(
            AssetHandle handle) const;
        [[nodiscard]] std::span<const AssetHandle> get_import_dependents(
            const std::filesystem::path& path) const;
        [[nodiscard]] std::vector<AssetRecord> get_assets() const;
        [[nodiscard]] AssetRevision get_revision(AssetHandle handle) const noexcept;
        [[nodiscard]] bool is_current(
            AssetHandle handle, AssetRevision revision) const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;

    private:
        [[nodiscard]] AssetRevision issue_revision();

        ProjectPaths m_paths;
        std::unordered_map<AssetHandle, AssetRecord> m_assets;
        std::unordered_map<std::filesystem::path, AssetHandle> m_handles_by_path;
        std::unordered_map<AssetHandle, std::vector<AssetHandle>>
            m_dependents_by_dependency;
        std::unordered_map<AssetHandle, std::vector<std::filesystem::path>>
            m_import_dependencies_by_asset;
        std::unordered_map<std::filesystem::path, std::vector<AssetHandle>>
            m_import_dependents_by_source;
        std::unordered_map<AssetHandle, std::uint64_t> m_asset_source_signatures;
        std::unordered_map<AssetHandle, AssetRevision> m_asset_revisions;
        AssetRevision m_next_revision = 1;
    };
}
