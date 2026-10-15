#include "asset/data/game_data.h"
#include "asset/data/animation_library.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "asset/default_assets.h"
#include "asset/database.h"
#include "asset/data/material_data.h"
#include "asset/data/shader_program_data.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/shader_program_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "diagnostics/profiler.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <variant>

namespace Comet {
    namespace {
        struct AssetCandidate {
            std::filesystem::path absolute_path;
            std::filesystem::path relative_path;
            AssetType expected_type = AssetType::Unknown;
            std::optional<AssetMetadata> metadata;
            std::variant<std::monostate, MaterialData, ShaderProgramData, GameData,
                AnimationLibrary, AnimationController, std::string>
                parsed_source;
        };

        struct ScanInput {
            std::filesystem::path path;
            std::filesystem::file_time_type write_time;
            std::uintmax_t size = 0;
            bool directory = false;
        };

        std::optional<ScanInput> read_scan_input(
            const std::filesystem::path& path, const bool directory) {
            std::error_code error;
            const auto write_time = std::filesystem::last_write_time(path, error);
            if(error)
                return std::nullopt;
            std::uintmax_t size = 0;
            if(!directory) {
                size = std::filesystem::file_size(path, error);
                if(error)
                    return std::nullopt;
            }
            return ScanInput{path, write_time, size, directory};
        }

        bool inputs_unchanged(const std::vector<ScanInput>& inputs) {
            return std::ranges::all_of(inputs, [](const ScanInput& input) {
                const auto current = read_scan_input(input.path, input.directory);
                return current && current->write_time == input.write_time
                       && current->size == input.size;
            });
        }

        using ImportDependenciesByAsset =
            std::unordered_map<AssetHandle, std::vector<std::filesystem::path>>;
        using ImportDependentsBySource =
            std::unordered_map<std::filesystem::path, std::vector<AssetHandle>>;

        std::string path_text(const std::filesystem::path& path) {
            return path.generic_string();
        }

        void add_issue(AssetScanReport& report, std::filesystem::path path, std::string message) {
            report.issues.push_back({.path = std::move(path), .message = std::move(message)});
        }

        void add_dependency_issues(AssetScanReport& report, const AssetRecord& owner,
            const MaterialData& data, const std::unordered_map<AssetHandle, AssetRecord>& assets) {
            const auto check = [&](const AssetHandle dependency_handle,
                                   const AssetType expected_type) {
                const auto dependency = assets.find(dependency_handle);
                if(dependency == assets.end()) {
                    add_issue(report, owner.path,
                        "material dependency handle " + std::to_string(dependency_handle.value())
                            + " is not indexed");
                    return;
                }
                if(dependency->second.type != expected_type) {
                    add_issue(report, owner.path,
                        "material dependency handle " + std::to_string(dependency_handle.value())
                            + " has type '" + std::string(to_string(dependency->second.type))
                            + "', expected '" + std::string(to_string(expected_type)) + "'");
                }
            };
            for(const auto& property : data.texture_properties)
                check(property.second, AssetType::Texture);
            if(data.shader_program)
                check(data.shader_program, AssetType::ShaderProgram);
        }
    }

    struct AssetDatabase::PreparedScan {
        std::filesystem::path assets_root;
        std::uint64_t database_generation = 0;
        AssetScanReport report;
        std::vector<AssetCandidate> candidates;
        std::vector<ScanInput> inputs;
        FileStates file_states;
        bool complete = false;
        bool stale = false;
    };

    AssetScanReport AssetDatabase::scan() {
        PROFILE_SCOPE("AssetDatabase::scan");
        if(m_read_only)
            return AssetScanReport{.indexed_assets = m_assets.size()};
        const auto index_path = m_paths.root() / ProductIndex::FILENAME;
        std::error_code error;
        const bool product = std::filesystem::exists(index_path, error);
        if(error)
            return AssetScanReport{.issues = {{index_path, error.message()}}};
        if(product) {
            auto index = ProductIndex::load(index_path);
            if(!index)
                return AssetScanReport{.issues = {{index_path, index.error()}}};
            auto opened = open_product_index(index.value());
            if(!opened)
                return AssetScanReport{.issues = {{index_path, opened.error()}}};
            AssetScanReport report{.indexed_assets = m_assets.size(), .snapshot_updated = true};
            for(const auto& asset : index.value().assets)
                report.added_assets.push_back(asset.handle);
            return report;
        }

        for(int attempt = 0; attempt < 2; ++attempt) {
            if(auto report = publish_scan(prepare_scan(m_paths, m_generation)))
                return std::move(*report);
        }
        AssetScanReport report;
        add_issue(report, m_paths.assets(), "assets changed during scan; retry Refresh");
        return report;
    }

    std::shared_ptr<AssetDatabase::PreparedScan> AssetDatabase::prepare_scan(
        ProjectPaths paths, const std::uint64_t database_generation) {
        PROFILE_SCOPE("AssetDatabase::prepare_scan");
        auto prepared = std::make_shared<PreparedScan>();
        auto& report = prepared->report;
        const std::filesystem::path assets_root = paths.assets();
        prepared->assets_root = assets_root;
        prepared->database_generation = database_generation;
        std::error_code error;
        const bool assets_exist = std::filesystem::exists(assets_root, error);
        if(error) {
            add_issue(report, assets_root, "failed to access assets directory: " + error.message());
            return prepared;
        }
        if(!assets_exist) {
            add_issue(report, assets_root, "assets directory does not exist");
            return prepared;
        }
        if(!std::filesystem::is_directory(assets_root, error)) {
            std::string message = "assets path is not a directory";
            if(error) {
                message = "failed to access assets directory: " + error.message();
            }
            add_issue(report, assets_root, std::move(message));
            return prepared;
        }

        auto root_input = read_scan_input(assets_root, true);
        if(!root_input) {
            add_issue(report, assets_root, "failed to inspect assets directory");
            return prepared;
        }
        prepared->inputs.push_back(std::move(*root_input));

        std::vector<std::filesystem::path> files;
        std::filesystem::recursive_directory_iterator iterator(
            assets_root, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        if(error) {
            add_issue(report, assets_root, "failed to scan assets directory: " + error.message());
            return prepared;
        }

        bool discovery_complete = true;
        while(iterator != end) {
            const std::filesystem::directory_entry entry = *iterator;
            std::error_code entry_error;
            if(entry.is_regular_file(entry_error)) {
                files.push_back(entry.path());
                auto input = read_scan_input(entry.path(), false);
                if(input)
                    prepared->inputs.push_back(std::move(*input));
                else {
                    discovery_complete = false;
                    add_issue(report, entry.path().lexically_relative(assets_root),
                        "failed to inspect file state");
                }
            } else if(!entry_error && entry.is_directory(entry_error)) {
                auto input = read_scan_input(entry.path(), true);
                if(input)
                    prepared->inputs.push_back(std::move(*input));
                else {
                    discovery_complete = false;
                    add_issue(report, entry.path().lexically_relative(assets_root),
                        "failed to inspect directory state");
                }
            } else if(entry_error) {
                discovery_complete = false;
                add_issue(report, entry.path().lexically_relative(assets_root),
                    "failed to inspect file: " + entry_error.message());
            }

            iterator.increment(error);
            if(error) {
                discovery_complete = false;
                add_issue(report, assets_root,
                    "failed while scanning assets directory: " + error.message());
                error.clear();
            }
        }

        if(!discovery_complete) {
            return prepared;
        }

        std::ranges::sort(
            files, {}, [](const std::filesystem::path& path) { return path.generic_string(); });

        std::unordered_map<std::filesystem::path, std::filesystem::path> sidecars;
        std::unordered_set<std::filesystem::path> source_paths;
        std::vector<std::filesystem::path> source_files;
        for(const std::filesystem::path& file : files) {
            if(file.filename() == ".DS_Store"
                || file.filename().string().starts_with(".comet-tmp-")) {
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

        for(const auto& [source, sidecar] : sidecars) {
            if(!source_paths.contains(source)) {
                add_issue(report, sidecar.lexically_relative(assets_root),
                    "metadata has no matching source asset");
            }
        }

        const MetadataSerializer serializer;
        auto& candidates = prepared->candidates;
        candidates.reserve(source_files.size());
        for(const std::filesystem::path& source : source_files) {
            const std::filesystem::path relative =
                source.lexically_relative(assets_root).lexically_normal();
            const auto expected_type = asset_type_from_path(source);
            if(!expected_type) {
                if(is_source_only_path(source)) {
                    continue;
                }
                add_issue(report, relative,
                    "unsupported asset extension '" + source.extension().string() + "'");
                continue;
            }

            AssetCandidate candidate{.absolute_path = source,
                .relative_path = relative,
                .expected_type = *expected_type};
            const auto sidecar = sidecars.find(source.lexically_normal());
            if(sidecar != sidecars.end()) {
                auto metadata = serializer.load(sidecar->second);
                if(!metadata) {
                    add_issue(
                        report, sidecar->second.lexically_relative(assets_root), metadata.error());
                    continue;
                }
                candidate.metadata = std::move(metadata).value();

                // 已有单网格资产保留身份；新 glTF 默认导入完整模型。
                if(candidate.expected_type == AssetType::Model
                    && candidate.metadata->type == AssetType::Mesh)
                    candidate.expected_type = AssetType::Mesh;

                if(candidate.metadata->type != candidate.expected_type) {
                    add_issue(report, sidecar->second.lexically_relative(assets_root),
                        "metadata type '" + std::string(to_string(candidate.metadata->type))
                            + "' does not match source type '"
                            + std::string(to_string(candidate.expected_type)) + "'");
                    continue;
                }
            }
            if(candidate.expected_type == AssetType::AnimationLibrary) {
                auto data = AnimationLibrary::load(source);
                if(data)
                    candidate.parsed_source = std::move(data).value();
                else
                    candidate.parsed_source = data.error();
            } else if(candidate.expected_type == AssetType::AnimationController) {
                auto data = AnimationControllerSerializer{}.load(source);
                if(data)
                    candidate.parsed_source = std::move(data).value();
                else
                    candidate.parsed_source = data.error();
            } else if(candidate.expected_type == AssetType::Data) {
                auto data = GameData::load(source);
                if(data)
                    candidate.parsed_source = std::move(data).value();
                else
                    candidate.parsed_source = data.error();
            } else if(candidate.expected_type == AssetType::Material) {
                auto material = MaterialSerializer{}.load(source);
                if(material)
                    candidate.parsed_source = std::move(material).value();
                else
                    candidate.parsed_source = material.error();
            } else if(candidate.expected_type == AssetType::ShaderProgram) {
                auto program = ShaderProgramSerializer{}.load(source);
                if(program)
                    candidate.parsed_source = std::move(program).value();
                else
                    candidate.parsed_source = program.error();
            }
            candidates.push_back(std::move(candidate));
        }

        prepared->stale = !inputs_unchanged(prepared->inputs);
        if(!prepared->stale) {
            prepared->file_states.reserve(prepared->inputs.size());
            for(const ScanInput& input : prepared->inputs) {
                if(!input.directory) {
                    prepared->file_states.emplace(input.path.lexically_normal(),
                        FileState{.write_time = input.write_time, .size = input.size});
                }
            }
        }
        prepared->complete = true;
        return prepared;
    }

    std::optional<AssetScanReport> AssetDatabase::publish_scan(
        std::shared_ptr<PreparedScan> prepared) {
        PROFILE_SCOPE("AssetDatabase::publish_scan");
        if(m_read_only)
            return AssetScanReport{.indexed_assets = m_assets.size(),
                .issues = {{m_paths.root(), "Product index is read-only"}}};

        if(!prepared || prepared->assets_root != m_paths.assets()
            || prepared->database_generation != m_generation)
            return std::nullopt;
        if(!prepared->complete)
            return std::move(prepared->report);
        if(prepared->stale)
            return std::nullopt;
        {
            PROFILE_SCOPE("AssetDatabase::publish_validate_inputs");
            if(!inputs_unchanged(prepared->inputs))
                return std::nullopt;
        }

        AssetScanReport report = std::move(prepared->report);
        auto& candidates = prepared->candidates;
        std::unordered_map<AssetHandle, AssetRecord> assets;
        std::unordered_map<std::filesystem::path, AssetHandle> handles_by_path;
        std::unordered_map<AssetHandle, std::vector<AssetHandle>> dependents_by_dependency;
        ImportDependenciesByAsset import_dependencies_by_asset;
        ImportDependentsBySource import_dependents_by_source;
        std::unordered_map<AssetHandle, std::uint64_t> asset_source_signatures;
        std::unordered_map<AssetHandle, AssetRevision> asset_revisions;
        AssetRevision next_revision = m_next_revision;
        const std::filesystem::path assets_root = m_paths.assets();

        std::unordered_map<AssetHandle, std::filesystem::path> known_handles;
        bool identity_conflict = false;
        for(const AssetCandidate& candidate : candidates) {
            if(!candidate.metadata) {
                continue;
            }

            const AssetHandle handle = candidate.metadata->handle;
            const auto previous = m_assets.find(handle);
            if(previous != m_assets.end() && previous->second.type != candidate.metadata->type) {
                add_issue(report, candidate.relative_path,
                    "asset guid " + std::to_string(handle.value()) + " cannot change type from '"
                        + std::string(to_string(previous->second.type)) + "' to '"
                        + std::string(to_string(candidate.metadata->type))
                        + "'; assign a new guid");
                identity_conflict = true;
                continue;
            }

            const auto [existing, inserted] =
                known_handles.emplace(handle, candidate.relative_path);
            if(!inserted) {
                add_issue(report, candidate.relative_path,
                    "duplicate guid " + std::to_string(handle.value()) + "; already used by '"
                        + path_text(existing->second) + "'");
                if(assets.at(handle).source_asset || !candidate.metadata->subresources.empty())
                    identity_conflict = true;
                continue;
            }

            const AssetRecord record{.handle = handle,
                .type = candidate.metadata->type,
                .path = candidate.relative_path,
                .import_settings = candidate.metadata->import_settings};
            assets.emplace(handle, record);
            handles_by_path.emplace(record.path, handle);
            for(const auto& child : candidate.metadata->subresources) {
                const auto previous_child = m_assets.find(child.handle);
                if(previous_child != m_assets.end()
                    && (previous_child->second.type != child.type
                        || previous_child->second.source_asset != handle)) {
                    add_issue(report, candidate.relative_path,
                        "model subresource guid cannot change type or source asset");
                    identity_conflict = true;
                    continue;
                }
                if(!known_handles.emplace(child.handle, candidate.relative_path).second) {
                    add_issue(report, candidate.relative_path,
                        "duplicate model subresource guid " + std::to_string(child.handle.value()));
                    identity_conflict = true;
                    continue;
                }
                assets.emplace(
                    child.handle, AssetRecord{.handle = child.handle,
                                      .type = child.type,
                                      .path = record.path,
                                      .import_settings = make_default_import_settings(child.type),
                                      .dependencies = {handle},
                                      .source_asset = handle,
                                      .source_key = child.key});
                dependents_by_dependency[handle].push_back(child.handle);
            }
        }

        if(identity_conflict) {
            return report;
        }

        const MetadataSerializer serializer;
        for(AssetCandidate& candidate : candidates) {
            if(candidate.metadata) {
                continue;
            }

            AssetHandle handle;
            do {
                handle = AssetHandle::generate();
            } while(known_handles.contains(handle) || m_assets.contains(handle));

            const AssetMetadata metadata{.handle = handle,
                .type = candidate.expected_type,
                .import_settings = make_default_import_settings(candidate.expected_type)};
            if(auto saved = serializer.save(metadata, metadata_path(candidate.absolute_path));
                !saved) {
                add_issue(report, candidate.relative_path, saved.error());
                continue;
            }

            known_handles.emplace(handle, candidate.relative_path);
            const AssetRecord record{.handle = handle,
                .type = candidate.expected_type,
                .path = candidate.relative_path,
                .import_settings = metadata.import_settings};
            assets.emplace(handle, record);
            handles_by_path.emplace(record.path, handle);
            ++report.generated_metadata;
        }

        for(const AssetCandidate& candidate : candidates) {
            if(candidate.expected_type != AssetType::Material)
                continue;
            const auto handle = handles_by_path.find(candidate.relative_path);
            if(handle == handles_by_path.end())
                continue;
            AssetRecord& record = assets.at(handle->second);
            const auto* material = std::get_if<MaterialData>(&candidate.parsed_source);
            if(!material) {
                add_issue(report, record.path, std::get<std::string>(candidate.parsed_source));
                continue;
            }
            record.dependencies = get_asset_dependencies(*material);

            add_dependency_issues(report, record, *material, assets);
            for(const AssetHandle dependency : record.dependencies) {
                dependents_by_dependency[dependency].push_back(record.handle);
            }
        }

        for(const AssetCandidate& candidate : candidates) {
            if(candidate.expected_type != AssetType::ShaderProgram)
                continue;
            const auto found = handles_by_path.find(candidate.relative_path);
            if(found == handles_by_path.end())
                continue;
            const AssetHandle handle = found->second;
            AssetRecord& record = assets.at(handle);
            const auto* program = std::get_if<ShaderProgramData>(&candidate.parsed_source);
            if(!program) {
                add_issue(report, record.path, std::get<std::string>(candidate.parsed_source));
                continue;
            }
            const auto index_stage = [&](const std::string_view stage,
                                         const std::string_view expected_extension,
                                         const AssetHandle source) {
                const auto dependency = assets.find(source);
                const auto label =
                    std::string(stage) + " source handle " + std::to_string(source.value());
                if(dependency == assets.end())
                    add_issue(report, record.path, label + " is not indexed");
                else if(dependency->second.type != AssetType::Shader)
                    add_issue(report, record.path, label + " must reference a Shader asset");
                else if(source_extension(dependency->second.path) != expected_extension)
                    add_issue(report, record.path,
                        label + " must reference a " + std::string(expected_extension) + " source");
                record.dependencies.push_back(source);
                dependents_by_dependency[source].push_back(handle);
            };
            index_stage("vertex", ".vert", program->vertex.source);
            index_stage("fragment", ".frag", program->fragment.source);
            std::ranges::sort(record.dependencies);
            record.dependencies.erase(
                std::ranges::unique(record.dependencies).begin(), record.dependencies.end());
        }

        for(const AssetCandidate& candidate : candidates) {
            if(candidate.expected_type != AssetType::Data)
                continue;
            const auto found = handles_by_path.find(candidate.relative_path);
            if(found == handles_by_path.end())
                continue;
            auto& record = assets.at(found->second);
            if(source_extension(record.path) == ".csv") {
                const auto schema = GameData::schema_path(record.path);
                import_dependencies_by_asset[record.handle] = {schema};
                import_dependents_by_source[schema].push_back(record.handle);
            }
            const auto* data = std::get_if<GameData>(&candidate.parsed_source);
            if(!data) {
                add_issue(report, record.path, std::get<std::string>(candidate.parsed_source));
                continue;
            }
            for(const auto& reference : data->references()) {
                const auto input = assets.find(reference.handle);
                const auto* builtin = find_default_asset(reference.handle);
                if((input == assets.end() && (!builtin || builtin->type != reference.type))
                    || (input != assets.end() && input->second.type != reference.type))
                    add_issue(report, record.path,
                        "Missing or mistyped data reference: "
                            + std::to_string(reference.handle.value()));
                record.dependencies.push_back(reference.handle);
                dependents_by_dependency[reference.handle].push_back(record.handle);
            }
            std::ranges::sort(record.dependencies);
            record.dependencies.erase(
                std::unique(record.dependencies.begin(), record.dependencies.end()),
                record.dependencies.end());
        }

        for(const auto& candidate : candidates) {
            if(candidate.expected_type != AssetType::AnimationLibrary
                && candidate.expected_type != AssetType::AnimationController)
                continue;
            const auto found = handles_by_path.find(candidate.relative_path);
            if(found == handles_by_path.end())
                continue;
            auto& record = assets.at(found->second);
            const auto add = [&](AssetHandle handle, AssetType type) {
                const auto input = assets.find(handle);
                const auto* builtin = find_default_asset(handle);
                if((input == assets.end() && (!builtin || builtin->type != type))
                    || (input != assets.end() && input->second.type != type))
                    add_issue(report, record.path,
                        "Missing or mistyped animation dependency: "
                            + std::to_string(handle.value()));
                record.dependencies.push_back(handle);
                dependents_by_dependency[handle].push_back(record.handle);
            };
            if(const auto* library = std::get_if<AnimationLibrary>(&candidate.parsed_source)) {
                for(const auto& entry : library->entries)
                    add(entry.mesh, AssetType::Mesh);
            } else if(const auto* controller =
                          std::get_if<AnimationController>(&candidate.parsed_source)) {
                if(controller->library)
                    add(controller->library, AssetType::AnimationLibrary);
            } else {
                add_issue(report, record.path, std::get<std::string>(candidate.parsed_source));
            }
            std::ranges::sort(record.dependencies);
            record.dependencies.erase(
                std::unique(record.dependencies.begin(), record.dependencies.end()),
                record.dependencies.end());
        }

        for(auto& dependency : dependents_by_dependency) {
            std::ranges::sort(dependency.second);
        }

        for(const auto& [handle, dependencies] : m_import_dependencies_by_asset) {
            if(!assets.contains(handle) || assets.at(handle).type == AssetType::Data) {
                continue;
            }
            import_dependencies_by_asset.emplace(handle, dependencies);
            for(const std::filesystem::path& dependency : dependencies) {
                import_dependents_by_source[dependency].push_back(handle);
            }
        }
        for(auto& dependent : import_dependents_by_source) {
            std::ranges::sort(dependent.second);
        }

        asset_source_signatures.reserve(assets.size());
        asset_revisions.reserve(assets.size());
        {
            PROFILE_SCOPE("AssetDatabase::publish_source_signatures");
            for(const auto& [handle, record] : assets) {
                asset_source_signatures.emplace(
                    handle, record_source_signature(record, record.dependencies, assets_root,
                                import_dependencies_by_asset, assets, &prepared->file_states));
            }
        }

        for(const auto& [handle, record] : assets) {
            const auto previous = m_assets.find(handle);
            const auto previous_signature = m_asset_source_signatures.find(handle);
            const auto previous_revision = m_asset_revisions.find(handle);
            const bool added = previous == m_assets.end();
            const bool changed = added || previous->second != record
                                 || previous_signature == m_asset_source_signatures.end()
                                 || previous_signature->second != asset_source_signatures.at(handle)
                                 || previous_revision == m_asset_revisions.end();
            if(!changed) {
                asset_revisions.emplace(handle, previous_revision->second);
                continue;
            }
            if(next_revision == std::numeric_limits<AssetRevision>::max()) {
                add_issue(report, record.path, "Asset revision counter exhausted");
                report.added_assets.clear();
                report.modified_assets.clear();
                return report;
            }
            (added ? report.added_assets : report.modified_assets).push_back(handle);
            asset_revisions.emplace(handle, next_revision++);
        }
        for(const auto& [handle, record] : m_assets) {
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
        m_import_dependencies_by_asset = std::move(import_dependencies_by_asset);
        m_import_dependents_by_source = std::move(import_dependents_by_source);
        m_asset_source_signatures = std::move(asset_source_signatures);
        m_asset_revisions = std::move(asset_revisions);
        m_next_revision = next_revision;
        ++m_generation;
        return report;
    }

}
