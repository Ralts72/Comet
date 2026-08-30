#include "asset/database.h"
#include "asset/material_data.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace Comet {
    namespace {
        struct AssetCandidate {
            std::filesystem::path absolute_path;
            std::filesystem::path relative_path;
            AssetType expected_type = AssetType::Unknown;
            std::optional<AssetMetadata> metadata;
        };

        std::optional<AssetType> asset_type_from_path(
            const std::filesystem::path& path) {
            std::string extension = path.extension().string();
            std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });

            if(extension == ".png" || extension == ".jpg"
               || extension == ".jpeg") {
                return AssetType::Texture;
            }
            if(extension == ".mat") return AssetType::Material;
            if(extension == ".obj" || extension == ".gltf"
               || extension == ".glb") {
                return AssetType::Mesh;
            }
            if(extension == ".vert" || extension == ".frag"
               || extension == ".comp" || extension == ".geom") {
                return AssetType::Shader;
            }
            if(extension == ".scene") return AssetType::Scene;
            return std::nullopt;
        }

        std::string path_text(const std::filesystem::path& path) {
            return path.generic_string();
        }

        std::uint64_t combine_source_signature(
            const std::uint64_t seed,
            const std::uint64_t value) noexcept {
            return seed ^ (value + 0x9e3779b97f4a7c15ULL
                + (seed << 6U) + (seed >> 2U));
        }

        std::uint64_t file_source_signature(
            const std::filesystem::path& path) {
            std::error_code error;
            const auto write_time = std::filesystem::last_write_time(path, error);
            if(error) {
                return 0;
            }

            std::uint64_t signature = static_cast<std::uint64_t>(
                write_time.time_since_epoch().count());
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if(!error) {
                signature = combine_source_signature(
                    signature,
                    static_cast<std::uint64_t>(size));
            }
            return signature;
        }

        std::uint64_t asset_source_signature(
            const std::filesystem::path& asset_path) {
            return combine_source_signature(
                file_source_signature(asset_path),
                file_source_signature(metadata_path(asset_path)));
        }

        void add_issue(
            AssetScanReport& report,
            std::filesystem::path path,
            std::string message) {
            report.issues.push_back({
                .path = std::move(path),
                .message = std::move(message)
            });
        }

        void add_dependency_issues(
            AssetScanReport& report,
            const AssetRecord& owner,
            const std::unordered_map<AssetHandle, AssetRecord>& assets) {
            for(const AssetHandle dependency_handle: owner.dependencies) {
                const auto dependency = assets.find(dependency_handle);
                if(dependency == assets.end()) {
                    add_issue(
                        report,
                        owner.path,
                        "material dependency handle "
                            + std::to_string(dependency_handle.value())
                            + " is not indexed");
                    continue;
                }
                if(dependency->second.type != AssetType::Texture) {
                    add_issue(
                        report,
                        owner.path,
                        "material dependency handle "
                            + std::to_string(dependency_handle.value())
                            + " has type '"
                            + std::string(to_string(dependency->second.type))
                            + "', expected 'texture'");
                }
            }
        }
    }

    AssetDatabase::AssetDatabase(ProjectPaths paths)
        : m_paths(std::move(paths)) {}

    AssetScanReport AssetDatabase::scan() {
        AssetScanReport report;
        std::unordered_map<AssetHandle, AssetRecord> assets;
        std::unordered_map<std::filesystem::path, AssetHandle> handles_by_path;
        std::unordered_map<AssetHandle, std::vector<AssetHandle>>
            dependents_by_dependency;
        std::unordered_map<AssetHandle, std::uint64_t> asset_source_signatures;
        std::unordered_map<AssetHandle, AssetRevision> asset_revisions;
        AssetRevision next_revision = m_next_revision;
        const auto issue_revision = [&next_revision]() {
            if(next_revision == std::numeric_limits<AssetRevision>::max()) {
                throw std::overflow_error("Asset revision counter exhausted");
            }
            return next_revision++;
        };

        const std::filesystem::path assets_root = m_paths.assets();
        std::error_code error;
        const bool assets_exist = std::filesystem::exists(assets_root, error);
        if(error) {
            add_issue(
                report,
                assets_root,
                "failed to access assets directory: " + error.message());
            return report;
        }
        if(!assets_exist) {
            add_issue(report, assets_root, "assets directory does not exist");
            return report;
        }
        if(!std::filesystem::is_directory(assets_root, error)) {
            add_issue(
                report,
                assets_root,
                error ? "failed to access assets directory: " + error.message()
                      : "assets path is not a directory");
            return report;
        }

        std::vector<std::filesystem::path> files;
        std::filesystem::recursive_directory_iterator iterator(
            assets_root,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::recursive_directory_iterator end;
        if(error) {
            add_issue(
                report,
                assets_root,
                "failed to scan assets directory: " + error.message());
            return report;
        }

        bool discovery_complete = true;
        while(iterator != end) {
            const std::filesystem::directory_entry entry = *iterator;
            std::error_code entry_error;
            if(entry.is_regular_file(entry_error)) {
                files.push_back(entry.path());
            } else if(entry_error) {
                discovery_complete = false;
                add_issue(
                    report,
                    entry.path().lexically_relative(assets_root),
                    "failed to inspect file: " + entry_error.message());
            }

            iterator.increment(error);
            if(error) {
                discovery_complete = false;
                add_issue(
                    report,
                    assets_root,
                    "failed while scanning assets directory: " + error.message());
                error.clear();
            }
        }

        if(!discovery_complete) {
            return report;
        }

        std::ranges::sort(files, {}, [](const std::filesystem::path& path) {
            return path.generic_string();
        });

        std::unordered_map<std::filesystem::path, std::filesystem::path> sidecars;
        std::unordered_set<std::filesystem::path> source_paths;
        std::vector<std::filesystem::path> source_files;
        for(const std::filesystem::path& file: files) {
            if(file.filename().string().starts_with(".comet-tmp-")) {
                continue;
            }
            if(file.extension() == ".meta") {
                std::filesystem::path source = file;
                source.replace_extension();
                sidecars.emplace(source.lexically_normal(), file);
            } else {
                source_files.push_back(file);
                source_paths.insert(file.lexically_normal());
            }
        }

        for(const auto& [source, sidecar]: sidecars) {
            if(!source_paths.contains(source)) {
                add_issue(
                    report,
                    sidecar.lexically_relative(assets_root),
                    "metadata has no matching source asset");
            }
        }

        const AssetMetadataSerializer serializer;
        std::vector<AssetCandidate> candidates;
        candidates.reserve(source_files.size());
        for(const std::filesystem::path& source: source_files) {
            const std::filesystem::path relative =
                source.lexically_relative(assets_root).lexically_normal();
            const auto expected_type = asset_type_from_path(source);
            if(!expected_type) {
                add_issue(
                    report,
                    relative,
                    "unsupported asset extension '" + source.extension().string() + "'");
                continue;
            }

            AssetCandidate candidate{
                .absolute_path = source,
                .relative_path = relative,
                .expected_type = *expected_type
            };
            const auto sidecar = sidecars.find(source.lexically_normal());
            if(sidecar != sidecars.end()) {
                try {
                    candidate.metadata = serializer.load(sidecar->second);
                } catch(const std::exception& exception) {
                    add_issue(
                        report,
                        sidecar->second.lexically_relative(assets_root),
                        exception.what());
                    continue;
                }

                if(candidate.metadata->type != candidate.expected_type) {
                    add_issue(
                        report,
                        sidecar->second.lexically_relative(assets_root),
                        "metadata type '"
                        + std::string(to_string(candidate.metadata->type))
                        + "' does not match source type '"
                        + std::string(to_string(candidate.expected_type)) + "'");
                    continue;
                }
            }
            candidates.push_back(std::move(candidate));
        }

        std::unordered_map<AssetHandle, std::filesystem::path> known_handles;
        for(const AssetCandidate& candidate: candidates) {
            if(!candidate.metadata) {
                continue;
            }

            const AssetHandle handle = candidate.metadata->handle;
            const auto [existing, inserted] = known_handles.emplace(
                handle, candidate.relative_path);
            if(!inserted) {
                add_issue(
                    report,
                    candidate.relative_path,
                    "duplicate guid " + std::to_string(handle.value())
                    + "; already used by '" + path_text(existing->second) + "'");
                continue;
            }

            const AssetRecord record{
                .handle = handle,
                .type = candidate.metadata->type,
                .path = candidate.relative_path,
                .import_settings = candidate.metadata->import_settings
            };
            assets.emplace(handle, record);
            handles_by_path.emplace(record.path, handle);
        }

        for(AssetCandidate& candidate: candidates) {
            if(candidate.metadata) {
                continue;
            }

            AssetHandle handle;
            do {
                handle = AssetHandle::generate();
            } while(known_handles.contains(handle));

            const AssetMetadata metadata{
                .handle = handle,
                .type = candidate.expected_type,
                .import_settings = make_default_import_settings(
                    candidate.expected_type)
            };
            try {
                serializer.save(metadata, metadata_path(candidate.absolute_path));
            } catch(const std::exception& exception) {
                add_issue(report, candidate.relative_path, exception.what());
                continue;
            }

            known_handles.emplace(handle, candidate.relative_path);
            const AssetRecord record{
                .handle = handle,
                .type = candidate.expected_type,
                .path = candidate.relative_path,
                .import_settings = metadata.import_settings
            };
            assets.emplace(handle, record);
            handles_by_path.emplace(record.path, handle);
            ++report.generated_metadata;
        }

        std::vector<AssetRecord*> material_records;
        for(auto& asset: assets) {
            AssetRecord& record = asset.second;
            if(record.type == AssetType::Material) {
                material_records.push_back(&record);
            }
        }
        std::ranges::sort(
            material_records,
            {},
            [](const AssetRecord* record) {
                return record->path.generic_string();
            });

        const MaterialSerializer material_serializer;
        for(AssetRecord* material_record: material_records) {
            try {
                const MaterialData data = material_serializer.load(
                    assets_root / material_record->path);
                material_record->dependencies = get_asset_dependencies(data);
            } catch(const std::exception& exception) {
                add_issue(report, material_record->path, exception.what());
                continue;
            }

            add_dependency_issues(report, *material_record, assets);
            for(const AssetHandle dependency:
                material_record->dependencies) {
                dependents_by_dependency[dependency].push_back(
                    material_record->handle);
            }
        }

        for(auto& dependency: dependents_by_dependency) {
            std::ranges::sort(dependency.second);
        }

        asset_source_signatures.reserve(assets.size());
        asset_revisions.reserve(assets.size());
        for(const auto& [handle, record]: assets) {
            asset_source_signatures.emplace(
                handle,
                asset_source_signature(assets_root / record.path));
        }

        for(const auto& [handle, record]: assets) {
            const auto previous = m_assets.find(handle);
            if(previous == m_assets.end()) {
                report.added_assets.push_back(handle);
                asset_revisions.emplace(handle, issue_revision());
                continue;
            }

            const auto previous_signature =
                m_asset_source_signatures.find(handle);
            const bool source_changed =
                previous_signature == m_asset_source_signatures.end()
                || previous_signature->second
                    != asset_source_signatures.at(handle);
            const auto previous_revision = m_asset_revisions.find(handle);
            if(previous->second != record || source_changed
               || previous_revision == m_asset_revisions.end()) {
                report.modified_assets.push_back(handle);
                asset_revisions.emplace(handle, issue_revision());
            } else {
                asset_revisions.emplace(handle, previous_revision->second);
            }
        }
        for(const auto& [handle, record]: m_assets) {
            static_cast<void>(record);
            if(!assets.contains(handle)) {
                report.removed_assets.push_back(handle);
            }
        }
        std::ranges::sort(report.added_assets);
        std::ranges::sort(report.removed_assets);
        std::ranges::sort(report.modified_assets);

        report.indexed_assets = assets.size();
        report.snapshot_updated = true;
        m_assets = std::move(assets);
        m_handles_by_path = std::move(handles_by_path);
        m_dependents_by_dependency = std::move(dependents_by_dependency);
        m_asset_source_signatures = std::move(asset_source_signatures);
        m_asset_revisions = std::move(asset_revisions);
        m_next_revision = next_revision;
        return report;
    }

    void AssetDatabase::update_import_settings(
        const AssetHandle handle,
        AssetImportSettings import_settings) {
        const auto asset = m_assets.find(handle);
        if(asset == m_assets.end()) {
            throw std::runtime_error(
                "Cannot update import settings for an unindexed asset handle "
                + std::to_string(handle.value()));
        }

        AssetRecord& record = asset->second;
        const AssetMetadata metadata{
            .handle = record.handle,
            .type = record.type,
            .import_settings = import_settings
        };
        AssetMetadataSerializer{}.save(
            metadata,
            metadata_path(m_paths.assets() / record.path));
        const bool settings_changed =
            record.import_settings != import_settings;
        record.import_settings = std::move(import_settings);
        const std::uint64_t source_signature = asset_source_signature(
            m_paths.assets() / record.path);
        const auto previous_signature = m_asset_source_signatures.find(handle);
        const bool source_changed =
            previous_signature == m_asset_source_signatures.end()
            || previous_signature->second != source_signature;
        m_asset_source_signatures[handle] = source_signature;
        if(settings_changed || source_changed) {
            m_asset_revisions[handle] = issue_revision();
        }
    }

    void AssetDatabase::update_dependencies(
        const AssetHandle handle,
        std::vector<AssetHandle> dependencies) {
        const auto asset = m_assets.find(handle);
        if(asset == m_assets.end()) {
            throw std::runtime_error(
                "Cannot update dependencies for an unindexed asset handle "
                + std::to_string(handle.value()));
        }

        if(std::ranges::any_of(dependencies, [](const AssetHandle dependency) {
               return !dependency;
           })) {
            throw std::runtime_error(
                "Cannot register an invalid dependency for asset handle "
                + std::to_string(handle.value()));
        }
        std::ranges::sort(dependencies);
        const auto duplicate = std::ranges::unique(dependencies);
        dependencies.erase(duplicate.begin(), duplicate.end());

        AssetRecord& record = asset->second;
        const bool dependencies_changed = record.dependencies != dependencies;
        for(const AssetHandle dependency: record.dependencies) {
            auto dependents = m_dependents_by_dependency.find(dependency);
            if(dependents == m_dependents_by_dependency.end()) {
                continue;
            }
            std::erase(dependents->second, handle);
            if(dependents->second.empty()) {
                m_dependents_by_dependency.erase(dependents);
            }
        }

        record.dependencies = std::move(dependencies);
        for(const AssetHandle dependency: record.dependencies) {
            auto& dependents = m_dependents_by_dependency[dependency];
            const auto position = std::ranges::lower_bound(dependents, handle);
            if(position == dependents.end() || *position != handle) {
                dependents.insert(position, handle);
            }
        }
        const std::uint64_t source_signature = asset_source_signature(
            m_paths.assets() / record.path);
        const auto previous_signature = m_asset_source_signatures.find(handle);
        const bool source_changed =
            previous_signature == m_asset_source_signatures.end()
            || previous_signature->second != source_signature;
        m_asset_source_signatures[handle] = source_signature;
        if(dependencies_changed || source_changed) {
            m_asset_revisions[handle] = issue_revision();
        }
    }

    const AssetRecord* AssetDatabase::find(const AssetHandle handle) const {
        const auto asset = m_assets.find(handle);
        return asset == m_assets.end() ? nullptr : &asset->second;
    }

    const AssetRecord* AssetDatabase::find(
        const std::filesystem::path& path) const {
        if(path.empty() || path.is_absolute()) {
            return nullptr;
        }

        const auto handle = m_handles_by_path.find(path.lexically_normal());
        return handle == m_handles_by_path.end() ? nullptr : find(handle->second);
    }

    std::span<const AssetHandle> AssetDatabase::get_dependencies(
        const AssetHandle handle) const {
        const AssetRecord* asset = find(handle);
        return asset ? std::span<const AssetHandle>(asset->dependencies)
                     : std::span<const AssetHandle>();
    }

    std::span<const AssetHandle> AssetDatabase::get_dependents(
        const AssetHandle handle) const {
        const auto dependents = m_dependents_by_dependency.find(handle);
        return dependents == m_dependents_by_dependency.end()
            ? std::span<const AssetHandle>()
            : std::span<const AssetHandle>(dependents->second);
    }

    std::vector<AssetRecord> AssetDatabase::get_assets() const {
        std::vector<AssetRecord> assets;
        assets.reserve(m_assets.size());
        for(const auto& entry: m_assets) {
            assets.push_back(entry.second);
        }
        std::ranges::sort(assets, {}, [](const AssetRecord& asset) {
            return asset.path.generic_string();
        });
        return assets;
    }

    AssetRevision AssetDatabase::get_revision(
        const AssetHandle handle) const noexcept {
        const auto revision = m_asset_revisions.find(handle);
        return revision == m_asset_revisions.end()
            ? INVALID_ASSET_REVISION
            : revision->second;
    }

    bool AssetDatabase::is_current(
        const AssetHandle handle,
        const AssetRevision revision) const noexcept {
        return revision != INVALID_ASSET_REVISION
            && get_revision(handle) == revision;
    }

    std::size_t AssetDatabase::size() const noexcept {
        return m_assets.size();
    }

    AssetRevision AssetDatabase::issue_revision() {
        if(m_next_revision == std::numeric_limits<AssetRevision>::max()) {
            throw std::overflow_error("Asset revision counter exhausted");
        }
        return m_next_revision++;
    }
}
