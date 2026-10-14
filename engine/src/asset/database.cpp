#include "asset/database.h"
#include "asset/serialization/metadata_serializer.h"
#include "diagnostics/profiler.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace Comet {
    namespace {
        using ImportDependenciesByAsset =
            std::unordered_map<AssetHandle, std::vector<std::filesystem::path>>;

        std::uint64_t combine_source_signature(
            const std::uint64_t seed, const std::uint64_t value) noexcept {
            return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
        }

        std::span<const std::filesystem::path> find_import_dependencies(
            const ImportDependenciesByAsset& dependencies_by_asset, const AssetHandle handle) {
            const auto dependencies = dependencies_by_asset.find(handle);
            if(dependencies == dependencies_by_asset.end()) {
                return {};
            }
            return std::span<const std::filesystem::path>(dependencies->second);
        }

        Result<std::filesystem::path> normalize_import_dependency(
            const std::filesystem::path& asset_root, const std::filesystem::path& dependency) {
            if(dependency.empty()) {
                return Result<std::filesystem::path>::failure(
                    "Import dependency path cannot be empty");
            }

            std::error_code error;
            const std::filesystem::path canonical_root =
                std::filesystem::weakly_canonical(asset_root, error);
            if(error) {
                return Result<std::filesystem::path>::failure(
                    "Failed to resolve assets directory: " + error.message());
            }
            const std::filesystem::path canonical_dependency = std::filesystem::weakly_canonical(
                dependency.is_absolute() ? dependency : canonical_root / dependency, error);
            if(error) {
                return Result<std::filesystem::path>::failure(
                    "Failed to resolve import dependency '" + dependency.string()
                    + "': " + error.message());
            }

            const std::filesystem::path relative =
                canonical_dependency.lexically_relative(canonical_root).lexically_normal();
            if(relative.empty() || relative == "." || relative.is_absolute()
                || *relative.begin() == "..") {
                return Result<std::filesystem::path>::failure(
                    "Import dependency must be inside the project assets directory: "
                    + dependency.string());
            }
            return Result<std::filesystem::path>::success(relative);
        }

    }

    std::uint64_t AssetDatabase::file_source_signature(
        const std::filesystem::path& path, const FileStates* files) {
        if(files) {
            const auto cached = files->find(path.lexically_normal());
            if(cached != files->end()) {
                const auto write_time = static_cast<std::uint64_t>(
                    cached->second.write_time.time_since_epoch().count());
                return combine_source_signature(
                    write_time, static_cast<std::uint64_t>(cached->second.size));
            }
        }

        std::error_code error;
        const auto write_time = std::filesystem::last_write_time(path, error);
        if(error)
            return 0;

        std::uint64_t signature = static_cast<std::uint64_t>(write_time.time_since_epoch().count());
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if(!error)
            signature = combine_source_signature(signature, static_cast<std::uint64_t>(size));
        return signature;
    }

    std::uint64_t AssetDatabase::asset_source_signature(const std::filesystem::path& asset_path,
        const std::filesystem::path& asset_root,
        const std::span<const std::filesystem::path> import_dependencies, const FileStates* files) {
        std::uint64_t signature = combine_source_signature(file_source_signature(asset_path, files),
            file_source_signature(metadata_path(asset_path), files));
        for(const std::filesystem::path& dependency : import_dependencies) {
            for(const unsigned char character : dependency.generic_string())
                signature = combine_source_signature(signature, character);
            signature = combine_source_signature(
                signature, file_source_signature(asset_root / dependency, files));
        }
        return signature;
    }

    std::uint64_t AssetDatabase::record_source_signature(const AssetRecord& record,
        const std::span<const AssetHandle> dependencies, const std::filesystem::path& assets_root,
        const ImportDependenciesByAsset& import_dependencies_by_asset,
        const std::unordered_map<AssetHandle, AssetRecord>& assets, const FileStates* files) {
        std::uint64_t signature = asset_source_signature(assets_root / record.path, assets_root,
            find_import_dependencies(import_dependencies_by_asset,
                record.source_asset ? record.source_asset : record.handle),
            files);
        if(record.type != AssetType::ShaderProgram)
            return signature;

        for(const AssetHandle source : dependencies) {
            const auto found = assets.find(source);
            const std::uint64_t source_signature =
                found == assets.end()
                    ? source.value()
                    : asset_source_signature(assets_root / found->second.path, assets_root,
                          find_import_dependencies(import_dependencies_by_asset, source), files);
            signature = combine_source_signature(signature, source_signature);
        }
        return signature;
    }

    AssetDatabase::AssetDatabase(ProjectPaths paths) : m_paths(std::move(paths)) {}

    Result<void> AssetDatabase::open_product_index(const ProductIndex& index) {
        if(m_generation != 0 || !m_assets.empty())
            return Result<void>::failure("Product index must be opened before using the database");
        if(auto valid = index.validate(); !valid)
            return valid;
        for(const auto& product : index.assets) {
            if(auto path = m_paths.resolve_asset_path(product.path); !path)
                return Result<void>::failure(path.error());
        }
        for(const auto& product : index.assets) {
            AssetRecord record{.handle = product.handle,
                .type = product.type,
                .path = product.path,
                .dependencies = product.dependencies};
            if(product.type == AssetType::Audio)
                record.import_settings = AudioImportSettings{product.audio_stream};
            m_assets.emplace(product.handle, std::move(record));
            m_handles_by_path.emplace(product.path, product.handle);
            m_asset_revisions.emplace(product.handle, m_next_revision++);
            for(const auto dependency : product.dependencies)
                m_dependents_by_dependency[dependency].push_back(product.handle);
            if(product.sampler)
                m_product_samplers.emplace(product.handle, *product.sampler);
        }
        m_read_only = true;
        ++m_generation;
        return Result<void>::success();
    }

    const SamplerDesc* AssetDatabase::product_sampler(AssetHandle handle) const {
        const auto found = m_product_samplers.find(handle);
        return found == m_product_samplers.end() ? nullptr : &found->second;
    }

    std::optional<AssetScanReport> AssetDatabase::scan_changed_sources(
        const std::span<const std::filesystem::path> paths) {
        if(m_read_only)
            return AssetScanReport{.indexed_assets = m_assets.size()};
        PROFILE_SCOPE("AssetDatabase::scan_changed_sources");
        std::unordered_set<AssetHandle> affected;
        const auto root = m_paths.assets();
        for(const auto& path : paths) {
            const auto normalized = path.lexically_normal();
            if(normalized.empty() || normalized == "." || normalized.is_absolute()
                || *normalized.begin() == ".." || normalized.extension() == ".meta")
                return std::nullopt;

            std::error_code error;
            if(!std::filesystem::is_regular_file(root / normalized, error) || error)
                return std::nullopt;

            const auto asset = m_handles_by_path.find(normalized);
            const auto importers = m_import_dependents_by_source.find(normalized);
            if(asset == m_handles_by_path.end() && importers == m_import_dependents_by_source.end())
                return std::nullopt;
            if(asset != m_handles_by_path.end()) {
                const auto type = m_assets.at(asset->second).type;
                if(type == AssetType::Material || type == AssetType::ShaderProgram)
                    return std::nullopt;
                affected.insert(asset->second);
            }
            if(importers != m_import_dependents_by_source.end())
                affected.insert(importers->second.begin(), importers->second.end());
        }

        const std::vector<AssetHandle> direct(affected.begin(), affected.end());
        for(const AssetHandle handle : direct) {
            const auto dependents = m_dependents_by_dependency.find(handle);
            if(dependents == m_dependents_by_dependency.end())
                continue;
            for(const AssetHandle dependent : dependents->second) {
                if(m_assets.at(dependent).type == AssetType::ShaderProgram
                    || m_assets.at(dependent).source_asset == handle)
                    affected.insert(dependent);
            }
        }

        AssetScanReport report;
        report.indexed_assets = m_assets.size();
        std::vector<std::pair<AssetHandle, std::uint64_t>> updates;
        for(const AssetHandle handle : affected) {
            const auto& record = m_assets.at(handle);
            const auto signature = record_source_signature(
                record, record.dependencies, root, m_import_dependencies_by_asset, m_assets);
            const auto previous = m_asset_source_signatures.find(handle);
            if(previous != m_asset_source_signatures.end() && previous->second == signature)
                continue;
            updates.emplace_back(handle, signature);
        }
        if(updates.size() > std::numeric_limits<AssetRevision>::max() - m_next_revision)
            return std::nullopt;
        for(const auto& [handle, signature] : updates) {
            m_asset_source_signatures[handle] = signature;
            m_asset_revisions[handle] = m_next_revision++;
            report.modified_assets.push_back(handle);
        }
        std::ranges::sort(report.modified_assets);
        report.snapshot_updated = !report.modified_assets.empty();
        if(report.snapshot_updated)
            ++m_generation;
        return report;
    }

    Result<void> AssetDatabase::update_import_settings(
        const AssetHandle handle, AssetImportSettings import_settings) {
        if(m_read_only)
            return Result<void>::failure("Product index is read-only");
        const auto asset = m_assets.find(handle);
        if(asset == m_assets.end())
            return Result<void>::failure(
                "Cannot update import settings for an unindexed asset handle "
                + std::to_string(handle.value()));

        auto& record = asset->second;
        if(record.source_asset || record.type == AssetType::Model)
            return Result<void>::failure(
                "Model subresource settings are owned by the model importer");
        const auto signature = record_source_signature(record, record.dependencies,
            m_paths.assets(), m_import_dependencies_by_asset, m_assets);
        const auto previous = m_asset_source_signatures.find(handle);
        if(record.import_settings == import_settings && previous != m_asset_source_signatures.end()
            && previous->second == signature)
            return Result<void>::success();
        if(m_next_revision == std::numeric_limits<AssetRevision>::max())
            return Result<void>::failure("Asset revision counter exhausted");

        const AssetMetadata metadata{
            .handle = record.handle, .type = record.type, .import_settings = import_settings};
        if(auto saved =
                MetadataSerializer{}.save(metadata, metadata_path(m_paths.assets() / record.path));
            !saved)
            return saved;
        record.import_settings = std::move(import_settings);
        m_asset_source_signatures[handle] = record_source_signature(record, record.dependencies,
            m_paths.assets(), m_import_dependencies_by_asset, m_assets);
        m_asset_revisions[handle] = m_next_revision++;
        ++m_generation;
        return Result<void>::success();
    }

    Result<void> AssetDatabase::update_dependencies(
        const AssetHandle handle, std::vector<AssetHandle> dependencies) {
        if(m_read_only)
            return Result<void>::failure("Product index is read-only");
        const auto asset = m_assets.find(handle);
        if(asset == m_assets.end()) {
            return Result<void>::failure("Cannot update dependencies for an unindexed asset handle "
                                         + std::to_string(handle.value()));
        }

        if(std::ranges::any_of(
               dependencies, [](const AssetHandle dependency) { return !dependency; })) {
            return Result<void>::failure("Cannot register an invalid dependency for asset handle "
                                         + std::to_string(handle.value()));
        }
        if(asset->second.source_asset)
            dependencies.push_back(asset->second.source_asset);
        std::ranges::sort(dependencies);
        const auto duplicate = std::ranges::unique(dependencies);
        dependencies.erase(duplicate.begin(), duplicate.end());

        AssetRecord& record = asset->second;
        const bool dependencies_changed = record.dependencies != dependencies;
        const std::uint64_t source_signature = record_source_signature(
            record, dependencies, m_paths.assets(), m_import_dependencies_by_asset, m_assets);
        const auto previous_signature = m_asset_source_signatures.find(handle);
        const bool source_changed = previous_signature == m_asset_source_signatures.end()
                                    || previous_signature->second != source_signature;
        if(!dependencies_changed && !source_changed)
            return Result<void>::success();
        if(m_next_revision == std::numeric_limits<AssetRevision>::max())
            return Result<void>::failure("Asset revision counter exhausted");
        for(const AssetHandle dependency : record.dependencies) {
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
        for(const AssetHandle dependency : record.dependencies) {
            auto& dependents = m_dependents_by_dependency[dependency];
            const auto position = std::ranges::lower_bound(dependents, handle);
            if(position == dependents.end() || *position != handle) {
                dependents.insert(position, handle);
            }
        }

        m_asset_source_signatures[handle] = source_signature;
        m_asset_revisions[handle] = m_next_revision++;
        ++m_generation;
        return Result<void>::success();
    }

    Result<void> AssetDatabase::update_import_dependencies(
        const AssetHandle handle, std::vector<std::filesystem::path> dependencies) {
        if(m_read_only)
            return Result<void>::failure("Product index is read-only");
        const auto asset = m_assets.find(handle);
        if(asset == m_assets.end()) {
            return Result<void>::failure(
                "Cannot update import dependencies for an unindexed asset handle "
                + std::to_string(handle.value()));
        }

        for(std::filesystem::path& dependency : dependencies) {
            auto normalized = normalize_import_dependency(m_paths.assets(), dependency);
            if(!normalized)
                return Result<void>::failure(normalized.error());
            dependency = std::move(normalized).value();
        }
        std::erase(dependencies, asset->second.path.lexically_normal());
        std::ranges::sort(dependencies, {},
            [](const std::filesystem::path& path) { return path.generic_string(); });
        const auto duplicate = std::ranges::unique(dependencies);
        dependencies.erase(duplicate.begin(), duplicate.end());

        const auto previous = m_import_dependencies_by_asset.find(handle);
        if(std::ranges::equal(
               find_import_dependencies(m_import_dependencies_by_asset, handle), dependencies)) {
            const auto signature =
                record_source_signature(asset->second, asset->second.dependencies, m_paths.assets(),
                    m_import_dependencies_by_asset, m_assets);
            const auto cached = m_asset_source_signatures.find(handle);
            if(cached != m_asset_source_signatures.end() && cached->second == signature)
                return Result<void>::success();
        }
        if(previous != m_import_dependencies_by_asset.end()) {
            for(const std::filesystem::path& dependency : previous->second) {
                auto dependents = m_import_dependents_by_source.find(dependency);
                if(dependents == m_import_dependents_by_source.end()) {
                    continue;
                }
                std::erase(dependents->second, handle);
                if(dependents->second.empty()) {
                    m_import_dependents_by_source.erase(dependents);
                }
            }
        }

        if(dependencies.empty()) {
            m_import_dependencies_by_asset.erase(handle);
        } else {
            m_import_dependencies_by_asset[handle] = dependencies;
        }
        for(const std::filesystem::path& dependency : dependencies) {
            auto& dependents = m_import_dependents_by_source[dependency];
            const auto position = std::ranges::lower_bound(dependents, handle);
            if(position == dependents.end() || *position != handle) {
                dependents.insert(position, handle);
            }
        }

        m_asset_source_signatures[handle] = record_source_signature(asset->second,
            asset->second.dependencies, m_paths.assets(), m_import_dependencies_by_asset, m_assets);
        if(asset->second.type == AssetType::Model)
            for(const auto child_handle : get_dependents(handle)) {
                const auto* child = find(child_handle);
                if(child && child->source_asset == handle)
                    m_asset_source_signatures[child_handle] = m_asset_source_signatures[handle];
            }
        ++m_generation;
        return Result<void>::success();
    }

    const AssetRecord* AssetDatabase::find(const AssetHandle handle) const {
        const auto asset = m_assets.find(handle);
        return asset == m_assets.end() ? nullptr : &asset->second;
    }

    const AssetRecord* AssetDatabase::find(const std::filesystem::path& path) const {
        if(path.empty() || path.is_absolute()) {
            return nullptr;
        }

        const auto handle = m_handles_by_path.find(path.lexically_normal());
        return handle == m_handles_by_path.end() ? nullptr : find(handle->second);
    }

    std::span<const AssetHandle> AssetDatabase::get_dependencies(const AssetHandle handle) const {
        const AssetRecord* asset = find(handle);
        if(!asset) {
            return {};
        }
        return std::span<const AssetHandle>(asset->dependencies);
    }

    std::span<const AssetHandle> AssetDatabase::get_dependents(const AssetHandle handle) const {
        const auto dependents = m_dependents_by_dependency.find(handle);
        if(dependents == m_dependents_by_dependency.end()) {
            return {};
        }
        return std::span<const AssetHandle>(dependents->second);
    }

    void AssetDatabase::include_dependents(std::unordered_set<AssetHandle>& handles) const {
        std::vector<AssetHandle> pending(handles.begin(), handles.end());
        for(std::size_t i = 0; i < pending.size(); ++i)
            for(const auto dependent : get_dependents(pending[i]))
                if(handles.insert(dependent).second)
                    pending.push_back(dependent);
    }

    std::span<const std::filesystem::path> AssetDatabase::get_import_dependencies(
        const AssetHandle handle) const {
        return find_import_dependencies(m_import_dependencies_by_asset, handle);
    }

    std::span<const AssetHandle> AssetDatabase::get_import_dependents(
        const std::filesystem::path& path) const {
        auto normalized = normalize_import_dependency(m_paths.assets(), path);
        if(!normalized)
            return {};
        const auto dependents = m_import_dependents_by_source.find(normalized.value());
        if(dependents == m_import_dependents_by_source.end()) {
            return {};
        }
        return std::span<const AssetHandle>(dependents->second);
    }

    std::vector<AssetRecord> AssetDatabase::get_assets() const {
        std::vector<AssetRecord> assets;
        assets.reserve(m_assets.size());
        for(const auto& entry : m_assets) {
            assets.push_back(entry.second);
        }
        std::ranges::sort(assets, [](const AssetRecord& left, const AssetRecord& right) {
            const auto left_path = left.path.generic_string(),
                       right_path = right.path.generic_string();
            if(left_path == right_path)
                return left.source_key < right.source_key;
            return left_path < right_path;
        });
        return assets;
    }

    AssetRevision AssetDatabase::get_revision(const AssetHandle handle) const noexcept {
        const auto revision = m_asset_revisions.find(handle);
        if(revision == m_asset_revisions.end()) {
            return INVALID_ASSET_REVISION;
        }
        return revision->second;
    }

    bool AssetDatabase::is_current(
        const AssetHandle handle, const AssetRevision revision) const noexcept {
        return revision != INVALID_ASSET_REVISION && get_revision(handle) == revision;
    }

    std::size_t AssetDatabase::size() const noexcept {
        return m_assets.size();
    }

}
