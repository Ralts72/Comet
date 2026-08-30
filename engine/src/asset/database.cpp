#include "asset/database.h"
#include "asset/serialization/metadata_serializer.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <optional>
#include <stdexcept>
#include <system_error>
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

        void add_issue(
            AssetScanReport& report,
            std::filesystem::path path,
            std::string message) {
            report.issues.push_back({
                .path = std::move(path),
                .message = std::move(message)
            });
        }
    }

    AssetDatabase::AssetDatabase(ProjectPaths paths)
        : m_paths(std::move(paths)) {}

    AssetScanReport AssetDatabase::scan() {
        AssetScanReport report;
        std::unordered_map<AssetHandle, AssetRecord> assets;
        std::unordered_map<std::filesystem::path, AssetHandle> handles_by_path;

        const std::filesystem::path assets_root = m_paths.assets();
        std::error_code error;
        const bool assets_exist = std::filesystem::exists(assets_root, error);
        if(error) {
            add_issue(
                report,
                assets_root,
                "failed to access assets directory: " + error.message());
            m_assets.clear();
            m_handles_by_path.clear();
            return report;
        }
        if(!assets_exist) {
            add_issue(report, assets_root, "assets directory does not exist");
            m_assets.clear();
            m_handles_by_path.clear();
            return report;
        }
        if(!std::filesystem::is_directory(assets_root, error)) {
            add_issue(
                report,
                assets_root,
                error ? "failed to access assets directory: " + error.message()
                      : "assets path is not a directory");
            m_assets.clear();
            m_handles_by_path.clear();
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
            m_assets.clear();
            m_handles_by_path.clear();
            return report;
        }

        while(iterator != end) {
            const std::filesystem::directory_entry entry = *iterator;
            std::error_code entry_error;
            if(entry.is_regular_file(entry_error)) {
                files.push_back(entry.path());
            } else if(entry_error) {
                add_issue(
                    report,
                    entry.path().lexically_relative(assets_root),
                    "failed to inspect file: " + entry_error.message());
            }

            iterator.increment(error);
            if(error) {
                add_issue(
                    report,
                    assets_root,
                    "failed while scanning assets directory: " + error.message());
                error.clear();
            }
        }

        std::ranges::sort(files, {}, [](const std::filesystem::path& path) {
            return path.generic_string();
        });

        std::unordered_map<std::filesystem::path, std::filesystem::path> sidecars;
        std::vector<std::filesystem::path> source_files;
        for(const std::filesystem::path& file: files) {
            if(file.extension() == ".meta") {
                std::filesystem::path source = file;
                source.replace_extension();
                sidecars.emplace(source.lexically_normal(), file);
            } else {
                source_files.push_back(file);
            }
        }

        for(const auto& [source, sidecar]: sidecars) {
            if(std::ranges::find(source_files, source) == source_files.end()) {
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

        report.indexed_assets = assets.size();
        m_assets = std::move(assets);
        m_handles_by_path = std::move(handles_by_path);
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
        record.import_settings = std::move(import_settings);
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

    std::size_t AssetDatabase::size() const noexcept {
        return m_assets.size();
    }
}
