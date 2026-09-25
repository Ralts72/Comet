#pragma once

#include "asset/metadata.h"
#include "common/result.h"
#include "common/export.h"
#include "core/project_paths.h"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
        struct PreparedScan;

        explicit AssetDatabase(ProjectPaths paths);
        [[nodiscard]] const ProjectPaths& paths() const noexcept { return m_paths; }

        [[nodiscard]] AssetScanReport scan();
        // 只读准备可在 Worker 执行；发布必须由数据库 owner 串行调用。
        [[nodiscard]] static std::shared_ptr<PreparedScan> prepare_scan(
            ProjectPaths paths, std::uint64_t database_generation);
        // 输入在准备后发生变化时返回空值；调用方应重新准备，不发布旧候选。
        [[nodiscard]] std::optional<AssetScanReport> publish_scan(
            std::shared_ptr<PreparedScan> prepared);
        [[nodiscard]] std::uint64_t generation() const noexcept { return m_generation; }
        // 仅更新已索引的非结构性源码；返回空值时调用方必须执行完整 scan。
        [[nodiscard]] std::optional<AssetScanReport> scan_changed_sources(
            std::span<const std::filesystem::path> paths);

        [[nodiscard]] Result<void> update_import_settings(
            AssetHandle handle, AssetImportSettings import_settings);
        [[nodiscard]] Result<void> update_dependencies(
            AssetHandle handle, std::vector<AssetHandle> dependencies);
        [[nodiscard]] Result<void> update_import_dependencies(
            AssetHandle handle, std::vector<std::filesystem::path> dependencies);

        [[nodiscard]] const AssetRecord* find(AssetHandle handle) const;
        [[nodiscard]] const AssetRecord* find(const std::filesystem::path& path) const;
        // 借用视图：数据库修改后，不可继续持有或遍历。
        [[nodiscard]] std::span<const AssetHandle> get_dependencies(AssetHandle handle) const;
        [[nodiscard]] std::span<const AssetHandle> get_dependents(AssetHandle handle) const;
        // 扩展为包含输入资产及所有传递依赖方的集合，允许缺失句柄和依赖环。
        void include_dependents(std::unordered_set<AssetHandle>& handles) const;
        [[nodiscard]] std::span<const std::filesystem::path> get_import_dependencies(
            AssetHandle handle) const;
        [[nodiscard]] std::span<const AssetHandle> get_import_dependents(
            const std::filesystem::path& path) const;
        [[nodiscard]] std::vector<AssetRecord> get_assets() const;
        [[nodiscard]] AssetRevision get_revision(AssetHandle handle) const noexcept;
        [[nodiscard]] bool is_current(AssetHandle handle, AssetRevision revision) const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;

    private:
        struct FileState {
            std::filesystem::file_time_type write_time;
            std::uintmax_t size = 0;
        };

        using FileStates = std::unordered_map<std::filesystem::path, FileState>;

        [[nodiscard]] static std::uint64_t file_source_signature(
            const std::filesystem::path& path, const FileStates* files);
        [[nodiscard]] static std::uint64_t asset_source_signature(
            const std::filesystem::path& asset_path, const std::filesystem::path& asset_root,
            std::span<const std::filesystem::path> import_dependencies, const FileStates* files);
        [[nodiscard]] static std::uint64_t record_source_signature(const AssetRecord& record,
            std::span<const AssetHandle> dependencies, const std::filesystem::path& assets_root,
            const std::unordered_map<AssetHandle, std::vector<std::filesystem::path>>&
                import_dependencies_by_asset,
            const std::unordered_map<AssetHandle, AssetRecord>& assets,
            const FileStates* files = nullptr);

        ProjectPaths m_paths;
        std::unordered_map<AssetHandle, AssetRecord> m_assets;
        std::unordered_map<std::filesystem::path, AssetHandle> m_handles_by_path;
        std::unordered_map<AssetHandle, std::vector<AssetHandle>> m_dependents_by_dependency;
        std::unordered_map<AssetHandle, std::vector<std::filesystem::path>>
            m_import_dependencies_by_asset;
        std::unordered_map<std::filesystem::path, std::vector<AssetHandle>>
            m_import_dependents_by_source;
        std::unordered_map<AssetHandle, std::uint64_t> m_asset_source_signatures;
        std::unordered_map<AssetHandle, AssetRevision> m_asset_revisions;
        AssetRevision m_next_revision = 1;
        std::uint64_t m_generation = 0;
    };
}
