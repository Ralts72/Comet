#include "asset/source_operations.h"

#include "common/result.h"
#include "common/file_io.h"
#include "common/scope_exit.h"
#include "asset/serialization/metadata_serializer.h"
#include "asset/serialization/material_serializer.h"
#include "asset/import/mesh_importer.h"
#include "asset/import/texture_importer.h"
#include "asset/import/environment_importer.h"
#include "scripting/script.h"
#include "audio/audio.h"
#include "diagnostics/logger.h"
#include <fastgltf/core.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <variant>

namespace Comet::AssetSourceOperations {
    namespace {
        static_assert(std::is_nothrow_move_assignable_v<AssetDatabase>);

        [[nodiscard]] AssetScanReport operation_error(
            std::filesystem::path path, std::string message) {
            AssetScanReport report;
            report.issues.push_back({.path = std::move(path), .message = std::move(message)});
            return report;
        }

        [[nodiscard]] bool is_safe_destination(const std::filesystem::path& path) {
            if(path.empty() || path.is_absolute() || path.filename().empty()) {
                return false;
            }

            const std::filesystem::path normalized = path.lexically_normal();
            return normalized != "." && normalized.begin() != normalized.end()
                   && *normalized.begin() != ".."
                   && !normalized.filename().string().starts_with(".comet-tmp-");
        }

        void remove_created_directories(const std::vector<std::filesystem::path>& directories) {
            for(const std::filesystem::path& directory : directories) {
                std::error_code error;
                static_cast<void>(std::filesystem::remove(directory, error));
            }
        }

        std::string extension_of(const std::filesystem::path& path) {
            auto extension = path.extension().string();
            std::ranges::transform(extension, extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }

        bool is_mesh_file(const std::filesystem::path& path) {
            const auto extension = extension_of(path);
            return extension == ".gltf" || extension == ".glb";
        }

        bool is_texture_file(const std::filesystem::path& path) {
            const auto extension = extension_of(path);
            return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
        }

        Result<void> validate_relative(const std::filesystem::path& path) {
            if(path.is_absolute() || path.has_root_name())
                return Result<void>::failure("Import path must be relative: " + path.string());
            for(const auto& part : path) {
                if(part == ".." || part.string().starts_with(".comet-tmp-"))
                    return Result<void>::failure("Unsupported import path: " + path.string());
            }
            return Result<void>::success();
        }

        Result<void> validate_inside(
            const std::filesystem::path& root, const std::filesystem::path& path) {
            std::error_code error;
            const auto canonical = std::filesystem::weakly_canonical(path, error);
            if(error)
                return Result<void>::failure(
                    "Cannot resolve import path: " + path.string() + ": " + error.message());
            const auto relative = canonical.lexically_relative(root);
            if(relative.empty())
                return Result<void>::failure("Cannot resolve import path: " + path.string());
            return validate_relative(relative);
        }

        Result<void> validate_available(const std::filesystem::path& path) {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if(error && error != std::errc::no_such_file_or_directory)
                return Result<void>::failure(
                    "Cannot inspect destination: " + path.string() + ": " + error.message());
            if(std::filesystem::exists(status) || std::filesystem::is_symlink(status))
                return Result<void>::failure(
                    "Destination already exists (not overwritten): " + path.string());
            return Result<void>::success();
        }

        Result<std::vector<std::filesystem::path>> gltf_dependencies(
            const std::filesystem::path& source_path) {
            using Dependencies = Result<std::vector<std::filesystem::path>>;
            auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
            if(!source)
                return Dependencies::failure(
                    std::string(fastgltf::getErrorMessage(source.error())));
            fastgltf::Parser parser;
            auto asset = parser.loadGltf(source.get(), source_path.parent_path(),
                fastgltf::Options::None, fastgltf::Category::Buffers | fastgltf::Category::Images);
            if(!asset)
                return Dependencies::failure(std::string(fastgltf::getErrorMessage(asset.error())));
            std::vector<std::filesystem::path> dependencies;
            const auto collect = [&](const auto& data) -> Result<void> {
                const auto* source_uri = std::get_if<fastgltf::sources::URI>(&data);
                if(!source_uri)
                    return Result<void>::success(); // 内嵌数据不需要复制独立文件。
                const auto& uri = source_uri->uri;
                if(!uri.isLocalPath() || !uri.scheme().empty() || !uri.query().empty()
                    || !uri.fragment().empty())
                    return Result<void>::failure(
                        "Only relative local glTF dependencies are supported");
                const auto relative = uri.fspath();
                if(auto result = validate_relative(relative); !result)
                    return result;
                if(relative.empty() || extension_of(relative) == ".meta")
                    return Result<void>::failure("Invalid glTF dependency: " + relative.string());
                dependencies.push_back(relative.lexically_normal());
                return Result<void>::success();
            };
            for(const auto& buffer : asset->buffers)
                if(auto result = collect(buffer.data); !result)
                    return Dependencies::failure(result.error());
            for(const auto& image : asset->images)
                if(auto result = collect(image.data); !result)
                    return Dependencies::failure(result.error());
            return Dependencies::success(std::move(dependencies));
        }
    }

    AssetScanReport import_files(AssetDatabase& database, const ProjectPaths& paths,
        const std::span<const std::filesystem::path> sources,
        const std::filesystem::path& directory) {
        std::filesystem::path staging;
        std::vector<std::filesystem::path> published;
        std::vector<std::filesystem::path> created_directories;
        bool scanned = false;
        AssetScanReport report;
        bool committed = false;
        const auto cleanup = [&](AssetScanReport* diagnostics) {
            const auto remove = [&](const std::filesystem::path& path) {
                std::error_code error;
                std::filesystem::remove(path, error);
                if(error && diagnostics)
                    diagnostics->issues.push_back({path, "Rollback failed: " + error.message()});
            };
            if(!committed) {
                for(auto it = published.rbegin(); it != published.rend(); ++it) {
                    if(scanned)
                        remove(metadata_path(*it));
                    remove(*it);
                }
                for(auto it = created_directories.rbegin(); it != created_directories.rend(); ++it)
                    remove(*it);
            }
            if(!staging.empty()) {
                std::error_code error;
                std::filesystem::remove_all(staging, error);
                if(error && diagnostics)
                    diagnostics->issues.push_back(
                        {staging, "Staging cleanup failed: " + error.message()});
            }
        };
        ScopeExit cleanup_on_exit([&] { cleanup(nullptr); });
        const auto execute_import = [&]() -> Result<void> {
            std::error_code error;
            if(auto result = validate_relative(directory); !result)
                return result;
            const auto root = std::filesystem::canonical(paths.assets(), error);
            if(error)
                return Result<void>::failure("Cannot resolve assets directory: " + error.message());
            const auto destination = root / directory;
            if(auto result = validate_inside(root, destination); !result)
                return result;
            if(!std::filesystem::is_directory(destination, error))
                return Result<void>::failure("Drop destination is not an existing directory"
                                             + (error ? ": " + error.message() : std::string{}));

            // key 是目标相对路径，value 是外部源；相同依赖只复制一次。
            std::map<std::filesystem::path, std::filesystem::path> files;
            std::vector<std::filesystem::path> roots;
            const auto add = [&](const std::filesystem::path& source,
                                 const std::filesystem::path& relative) -> Result<void> {
                if(auto result = validate_relative(relative); !result)
                    return result;
                if(!std::filesystem::is_regular_file(source, error))
                    return Result<void>::failure(
                        "Missing or unreadable import file: " + source.string());
                const auto canonical = std::filesystem::canonical(source, error);
                if(error)
                    return Result<void>::failure(
                        "Cannot resolve import file '" + source.string() + "': " + error.message());
                const auto within_project = canonical.lexically_relative(root);
                if(!within_project.empty() && *within_project.begin() != "..")
                    return Result<void>::failure(
                        "File already belongs to this project: " + source.string());
                const auto [it, inserted] = files.emplace(relative, canonical);
                if(!inserted && it->second != canonical)
                    return Result<void>::failure(
                        "Dropped files have conflicting names: " + relative.string());
                return Result<void>::success();
            };
            for(const auto& source : sources) {
                if(!is_mesh_file(source) && !is_texture_file(source)
                    && extension_of(source) != ".hdr" && extension_of(source) != ".lua"
                    && extension_of(source) != ".wav")
                    continue;
                if(!source.is_absolute())
                    return Result<void>::failure("Dropped file path must be absolute");
                const auto relative = source.filename();
                if(auto result = add(source, relative); !result)
                    return result;
                if(std::ranges::find(roots, relative) == roots.end())
                    roots.push_back(relative);
                if(is_mesh_file(source)) {
                    const auto parent = std::filesystem::canonical(source.parent_path(), error);
                    if(error)
                        return Result<void>::failure(
                            "Cannot resolve model directory: " + error.message());
                    auto dependencies = gltf_dependencies(source);
                    if(!dependencies)
                        return Result<void>::failure(dependencies.error());
                    for(const auto& dependency : dependencies.value()) {
                        if(auto result = validate_inside(parent, source.parent_path() / dependency);
                            !result)
                            return result;
                        if(auto result = add(source.parent_path() / dependency, dependency);
                            !result)
                            return result;
                    }
                }
            }
            if(roots.empty())
                return Result<void>::failure(
                    "Drop PNG/JPEG textures, HDR environments, Lua scripts, WAV audio or glTF/GLB models (not directories)");
            for(const auto& source : sources) {
                if(is_mesh_file(source) || is_texture_file(source) || extension_of(source) == ".hdr"
                    || extension_of(source) == ".lua" || extension_of(source) == ".wav")
                    continue;
                if(extension_of(source) == ".meta") {
                    auto owner = source;
                    owner.replace_extension();
                    if(std::ranges::find(sources, owner) != sources.end())
                        continue;
                }
                const auto canonical = std::filesystem::canonical(source, error);
                if(error)
                    return Result<void>::failure(
                        "Cannot resolve import file '" + source.string() + "': " + error.message());
                if(std::ranges::none_of(
                       files, [&](const auto& file) { return file.second == canonical; }))
                    return Result<void>::failure("Unsupported standalone file: " + source.string());
            }
            for(const auto& [relative, source] : files) {
                if(auto result = validate_inside(root, destination / relative); !result)
                    return result;
                if(auto result = validate_available(destination / relative); !result)
                    return result;
                if(auto result = validate_available(metadata_path(destination / relative)); !result)
                    return result;
            }

            const auto staging_parent = paths.cache() / "file-import";
            std::filesystem::create_directories(staging_parent, error);
            if(error)
                return Result<void>::failure(
                    "Cannot create import staging directory: " + error.message());
            auto candidate = staging_parent / std::to_string(AssetHandle::generate().value());
            if(!std::filesystem::create_directory(candidate, error))
                return Result<void>::failure("Cannot reserve file import staging directory"
                                             + (error ? ": " + error.message() : std::string{}));
            staging = std::move(candidate);
            for(const auto& [relative, source] : files) {
                const auto target = staging / relative;
                std::filesystem::create_directories(target.parent_path(), error);
                if(error)
                    return Result<void>::failure(
                        "Cannot create staging subdirectory: " + error.message());
                std::filesystem::copy_file(source, target, error);
                if(error)
                    return Result<void>::failure(
                        "Cannot copy import file '" + source.string() + "': " + error.message());
            }
            for(const auto& relative : roots) {
                if(is_mesh_file(relative)) {
                    // 再检查暂存副本，拒绝复制期间改变了依赖列表的源文件。
                    auto dependencies = gltf_dependencies(staging / relative);
                    if(!dependencies)
                        return Result<void>::failure(dependencies.error());
                    for(const auto& dependency : dependencies.value()) {
                        if(!files.contains(dependency))
                            return Result<void>::failure(
                                "glTF dependencies changed during copy; retry import");
                    }
                    if(auto result = MeshImporter{}.import(staging / relative); !result)
                        return Result<void>::failure(result.error());
                } else if(extension_of(relative) == ".lua") {
                    if(auto script = Script::load(staging / relative); !script)
                        return Result<void>::failure(script.error().message);
                } else if(extension_of(relative) == ".wav") {
                    if(auto clip = AudioClip::load(staging / relative); !clip)
                        return Result<void>::failure(clip.error().message);
                } else if(extension_of(relative) == ".hdr") {
                    if(auto result = EnvironmentImporter{}.validate_source(staging / relative);
                        !result)
                        return Result<void>::failure(result.error());
                } else {
                    if(auto result = TextureImporter{}.import(staging / relative); !result)
                        return Result<void>::failure(result.error());
                }
            }

            published.reserve(files.size());
            for(const auto& [relative, source] : files) {
                const auto target = destination / relative;
                if(auto result = validate_inside(root, target); !result)
                    return result;
                if(auto result = validate_available(metadata_path(target)); !result)
                    return result;
                std::vector<std::filesystem::path> missing;
                for(auto parent = target.parent_path(); !parent.empty();
                    parent = parent.parent_path()) {
                    const bool exists = std::filesystem::exists(parent, error);
                    if(error)
                        return Result<void>::failure(
                            "Cannot inspect import destination: " + error.message());
                    if(exists)
                        break;
                    missing.push_back(parent);
                }
                for(auto it = missing.rbegin(); it != missing.rend(); ++it) {
                    created_directories.push_back(*it);
                    if(!std::filesystem::create_directory(*it, error))
                        created_directories.pop_back();
                    if(error)
                        return Result<void>::failure(
                            "Cannot create import destination: " + error.message());
                }
                // 同卷硬链接原子发布单个文件，并且不会覆盖竞态中新出现的目标。
                published.push_back(target);
                std::filesystem::create_hard_link(staging / relative, target, error);
                if(error) {
                    published.pop_back();
                    return Result<void>::failure("Cannot publish imported file '" + target.string()
                                                 + "': " + error.message());
                }
            }
            AssetDatabase candidate_database = database;
            scanned = true;
            report = candidate_database.scan();
            bool indexed = report.snapshot_updated && report.succeeded();
            for(const auto& relative : roots)
                indexed = indexed && candidate_database.find(directory / relative);
            if(!indexed)
                return Result<void>::failure(
                    "Imported files could not be indexed; import rolled back");
            database = std::move(candidate_database);
            committed = true;
            return Result<void>::success();
        };
        const auto result = execute_import();
        if(!result) {
            report.snapshot_updated = false;
            report.indexed_assets = database.size();
            report.generated_metadata = 0;
            report.added_assets.clear();
            report.removed_assets.clear();
            report.modified_assets.clear();
            report.issues.push_back({directory, result.error()});
        }
        cleanup(&report);
        cleanup_on_exit.release();
        return report;
    }

    namespace {
        AssetScanReport create_text_asset(AssetDatabase& database, const ProjectPaths& paths,
            const std::filesystem::path& destination, const std::string_view contents,
            const AssetType type, const std::string_view extension, const std::string_view name) {
            if(!is_safe_destination(destination) || extension_of(destination) != extension)
                return operation_error(destination, std::string(name)
                                                        + " destination must be a relative "
                                                        + std::string(extension) + " path");
            if(auto valid = validate_relative(destination); !valid)
                return operation_error(destination, valid.error());
            std::error_code error;
            const auto root = std::filesystem::canonical(paths.assets(), error);
            if(error)
                return operation_error(
                    destination, "Cannot resolve assets directory: " + error.message());
            const auto target = root / destination;
            const auto meta = metadata_path(target);
            if(auto valid = validate_inside(root, target); !valid)
                return operation_error(destination, valid.error());
            if(!std::filesystem::is_directory(target.parent_path(), error))
                return operation_error(
                    destination, std::string(name) + " directory does not exist");
            for(const auto& path : {target, meta})
                if(auto valid = validate_available(path); !valid)
                    return operation_error(destination, valid.error());

            const auto handle = AssetHandle::generate();
            std::string staging_name(name);
            std::ranges::transform(staging_name, staging_name.begin(),
                [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            const auto staging_parent = paths.cache() / (staging_name + "-create");
            std::filesystem::create_directories(staging_parent, error);
            if(error)
                return operation_error(
                    destination, "Cannot create staging directory: " + error.message());
            const auto staging = staging_parent / std::to_string(handle.value());
            if(!std::filesystem::create_directory(staging, error))
                return operation_error(
                    destination, "Cannot reserve " + staging_name + " staging directory");

            AssetScanReport report;
            bool source_published = false;
            bool metadata_published = false;
            bool committed = false;
            const auto cleanup = [&] {
                const auto remove = [&](const std::filesystem::path& path) {
                    std::error_code failure;
                    std::filesystem::remove(path, failure);
                    if(failure)
                        report.issues.push_back(
                            {path, std::string(name) + " rollback failed: " + failure.message()});
                };
                if(!committed) {
                    if(source_published)
                        remove(target);
                    if(metadata_published)
                        remove(meta);
                }
                std::error_code failure;
                std::filesystem::remove_all(staging, failure);
                if(failure)
                    report.issues.push_back(
                        {staging, "Staging cleanup failed: " + failure.message()});
            };
            ScopeExit cleanup_on_exit(cleanup);
            const auto publish = [&]() -> Result<void> {
                const auto staged_source = staging / ("asset" + std::string(extension));
                const auto staged_metadata = metadata_path(staged_source);
                if(auto saved = write_text_file_atomic(staged_source, contents); !saved)
                    return saved;
                if(auto saved = MetadataSerializer{}.save(
                       {.handle = handle,
                           .type = type,
                           .import_settings = make_default_import_settings(type)},
                       staged_metadata);
                    !saved)
                    return saved;
                // 只发布本次创建的文件，不覆盖并发创建的目标。
                std::filesystem::create_hard_link(staged_metadata, meta, error);
                if(error)
                    return Result<void>::failure(
                        "Cannot publish " + staging_name + " metadata: " + error.message());
                metadata_published = true;
                std::filesystem::create_hard_link(staged_source, target, error);
                if(error)
                    return Result<void>::failure(
                        "Cannot publish " + staging_name + ": " + error.message());
                source_published = true;
                AssetDatabase candidate = database;
                report = candidate.scan();
                const auto* record = candidate.find(handle);
                if(!report.snapshot_updated || !report.succeeded() || !record
                    || record->path != destination.lexically_normal() || record->type != type)
                    return Result<void>::failure(
                        std::string(name) + " could not be indexed; creation rolled back");
                database = std::move(candidate);
                committed = true;
                return Result<void>::success();
            };
            if(auto published = publish(); !published) {
                report = operation_error(destination, published.error());
                report.indexed_assets = database.size();
            }
            cleanup();
            cleanup_on_exit.release();
            return report;
        }
    }

    AssetScanReport create_material(AssetDatabase& database, const ProjectPaths& paths,
        const std::filesystem::path& destination, const MaterialData& data) {
        auto serialized = MaterialSerializer{}.serialize(data);
        if(!serialized)
            return operation_error(destination, serialized.error());
        return create_text_asset(database, paths, destination, serialized.value(),
            AssetType::Material, ".mat", "Material");
    }

    AssetScanReport create_script(AssetDatabase& database, const ProjectPaths& paths,
        const std::filesystem::path& destination) {
        constexpr std::string_view source = R"(local script = {}

function script:update(dt)
end

return script
)";
        if(auto valid = Script::create(std::string(source), destination.generic_string()); !valid)
            return operation_error(destination, valid.error().message);
        return create_text_asset(
            database, paths, destination, source, AssetType::Script, ".lua", "Script");
    }

    AssetScanReport remove_asset(
        AssetDatabase& database, const ProjectPaths& paths, const AssetHandle handle) {
        const auto* indexed = database.find(handle);
        if(!indexed)
            return operation_error({}, "Asset is not indexed");
        const AssetRecord record = *indexed;
        if(!database.get_dependents(handle).empty())
            return operation_error(record.path, "Asset is referenced by another indexed asset");
        if(!is_safe_destination(record.path))
            return operation_error(record.path, "Asset path is not a safe relative path");

        const auto source = paths.assets() / record.path;
        const auto metadata_file = metadata_path(source);
        const auto metadata = MetadataSerializer{}.load(metadata_file);
        if(!metadata || metadata.value().handle != handle || metadata.value().type != record.type)
            return operation_error(
                record.path, "Asset metadata does not match its indexed identity");

        std::error_code error;
        const auto project_root = std::filesystem::canonical(paths.root(), error);
        if(error)
            return operation_error(record.path, "Cannot resolve project root: " + error.message());
        const auto assets_root = std::filesystem::canonical(paths.assets(), error);
        if(error)
            return operation_error(
                record.path, "Cannot resolve assets directory: " + error.message());
        for(const auto& path : {source, metadata_file}) {
            if(auto valid = validate_inside(assets_root, path); !valid)
                return operation_error(record.path, valid.error());
            const auto status = std::filesystem::symlink_status(path, error);
            if(error || !std::filesystem::is_regular_file(status))
                return operation_error(
                    record.path, "Asset source and metadata must be regular files");
        }

        const auto trash_root = paths.local_data() / "trash";
        if(auto valid = validate_inside(project_root, trash_root); !valid)
            return operation_error(record.path, valid.error());
        std::filesystem::create_directories(trash_root, error);
        if(error)
            return operation_error(record.path, "Cannot create project trash: " + error.message());
        const auto trash_entry = trash_root / std::to_string(AssetHandle::generate().value());
        if(!std::filesystem::create_directory(trash_entry, error))
            return operation_error(record.path, "Cannot reserve project trash entry");
        const auto trashed_source = trash_entry / record.path;
        const auto trashed_metadata = metadata_path(trashed_source);

        AssetScanReport report;
        bool source_moved = false;
        bool metadata_moved = false;
        std::error_code source_restore_error;
        std::error_code metadata_restore_error;
        const auto rollback = [&] {
            if(metadata_moved)
                std::filesystem::rename(trashed_metadata, metadata_file, metadata_restore_error);
            if(source_moved)
                std::filesystem::rename(trashed_source, source, source_restore_error);
            if(!source_restore_error && !metadata_restore_error) {
                std::error_code cleanup_error;
                std::filesystem::remove_all(trash_entry, cleanup_error);
                if(cleanup_error)
                    report.issues.push_back({trash_entry,
                        "Cannot clean project trash entry: " + cleanup_error.message()});
            }
        };
        ScopeExit rollback_on_exit(rollback);
        AssetDatabase candidate = database;
        const auto remove = [&]() -> Result<void> {
            std::filesystem::create_directories(trashed_source.parent_path(), error);
            if(error)
                return Result<void>::failure("Cannot prepare project trash: " + error.message());
            std::filesystem::rename(source, trashed_source, error);
            if(error)
                return Result<void>::failure(
                    "Cannot move asset to project trash: " + error.message());
            source_moved = true;
            std::filesystem::rename(metadata_file, trashed_metadata, error);
            if(error)
                return Result<void>::failure(
                    "Cannot move asset metadata to project trash: " + error.message());
            metadata_moved = true;
            report = candidate.scan();
            if(!report.snapshot_updated || !report.succeeded() || candidate.find(handle)
                || std::ranges::find(report.removed_assets, handle) == report.removed_assets.end())
                return Result<void>::failure("Asset removal could not be indexed");
            return Result<void>::success();
        };
        const auto result = remove();
        if(result) {
            database = std::move(candidate);
            rollback_on_exit.release();
            LOG_INFO("Moved asset '{}' to project trash '{}'", record.path.generic_string(),
                trash_entry.generic_string());
            return report;
        }
        rollback();
        rollback_on_exit.release();
        report.snapshot_updated = false;
        report.indexed_assets = database.size();
        report.added_assets.clear();
        report.removed_assets.clear();
        report.modified_assets.clear();
        report.issues.push_back({record.path, result.error()});
        if(source_restore_error || metadata_restore_error)
            report.issues.push_back({trash_entry,
                "Asset rollback was incomplete; recover source and metadata from project trash"});
        return report;
    }

    AssetScanReport move(AssetDatabase& database, const ProjectPaths& paths,
        const AssetHandle handle, const std::filesystem::path& destination) {
        if(!handle) {
            return operation_error(destination, "cannot move an invalid asset handle");
        }
        if(!is_safe_destination(destination)) {
            return operation_error(destination,
                "asset destination must be a project-relative file path inside assets");
        }

        const AssetRecord* indexed_record = database.find(handle);
        if(!indexed_record) {
            return operation_error(
                destination, "asset handle " + std::to_string(handle.value()) + " is not indexed");
        }
        const AssetRecord record = *indexed_record;
        const std::filesystem::path source_relative = record.path.lexically_normal();
        const std::filesystem::path destination_relative = destination.lexically_normal();
        if(source_relative == destination_relative) {
            return operation_error(
                destination_relative, "asset source and destination paths are identical");
        }
        if(source_relative.extension() != destination_relative.extension()) {
            return operation_error(
                destination_relative, "asset move cannot change the source file extension");
        }

        const std::filesystem::path asset_root = paths.assets();
        const std::filesystem::path source = asset_root / source_relative;
        const std::filesystem::path source_metadata = metadata_path(source);
        const std::filesystem::path target = asset_root / destination_relative;
        const std::filesystem::path target_metadata = metadata_path(target);

        const auto metadata = MetadataSerializer{}.load(source_metadata);
        if(!metadata) {
            return operation_error(
                source_relative, "cannot move asset with invalid metadata: " + metadata.error());
        }
        if(metadata.value().handle != handle || metadata.value().type != record.type) {
            return operation_error(source_relative,
                "source metadata does not match the indexed asset identity and type");
        }

        std::error_code error;
        const bool source_exists = std::filesystem::is_regular_file(source, error);
        if(error || !source_exists) {
            std::string message = "asset source is not a regular file";
            if(error) {
                message = "failed to access asset source: " + error.message();
            }
            return operation_error(source_relative, std::move(message));
        }

        const bool target_exists = std::filesystem::exists(target, error);
        if(error || target_exists) {
            std::string message = "asset destination already exists";
            if(error) {
                message = "failed to inspect asset destination: " + error.message();
            }
            return operation_error(destination_relative, std::move(message));
        }
        const bool target_metadata_exists = std::filesystem::exists(target_metadata, error);
        if(error || target_metadata_exists) {
            std::string message = "destination metadata already exists";
            if(error) {
                message = "failed to inspect destination metadata: " + error.message();
            }
            return operation_error(metadata_path(destination_relative), std::move(message));
        }

        const std::filesystem::path canonical_root =
            std::filesystem::weakly_canonical(asset_root, error);
        if(error) {
            return operation_error(
                destination_relative, "failed to resolve assets directory: " + error.message());
        }
        const std::filesystem::path canonical_parent =
            std::filesystem::weakly_canonical(target.parent_path(), error);
        if(error) {
            return operation_error(destination_relative,
                "failed to resolve destination directory: " + error.message());
        }
        const std::filesystem::path parent_relative =
            canonical_parent.lexically_relative(canonical_root).lexically_normal();
        if(parent_relative.is_absolute()
            || (!parent_relative.empty() && parent_relative != "."
                && *parent_relative.begin() == "..")) {
            return operation_error(
                destination_relative, "asset destination resolves outside the assets directory");
        }

        AssetDatabase candidate_database = database;
        std::vector<std::filesystem::path> created_directories;
        for(std::filesystem::path directory = target.parent_path();
            directory != asset_root && !directory.empty(); directory = directory.parent_path()) {
            const bool exists = std::filesystem::exists(directory, error);
            if(error) {
                return operation_error(destination_relative,
                    "failed to inspect destination directory: " + error.message());
            }
            if(exists) {
                break;
            }
            created_directories.push_back(directory);
        }
        bool source_moved = false;
        bool metadata_moved = false;
        std::error_code source_rollback_error;
        std::error_code metadata_rollback_error;
        const auto rollback = [&] {
            if(metadata_moved) {
                std::filesystem::rename(target_metadata, source_metadata, metadata_rollback_error);
                metadata_moved = false;
            }
            if(source_moved) {
                std::filesystem::rename(target, source, source_rollback_error);
                source_moved = false;
            }
            remove_created_directories(created_directories);
        };
        ScopeExit rollback_on_exit(rollback);
        const auto execute_move = [&]() -> AssetScanReport {
            std::filesystem::create_directories(target.parent_path(), error);
            if(error)
                return operation_error(destination_relative,
                    "failed to create destination directory: " + error.message());

            std::filesystem::rename(source, target, error);
            if(error)
                return operation_error(
                    destination_relative, "failed to move asset source: " + error.message());
            source_moved = true;

            std::filesystem::rename(source_metadata, target_metadata, error);
            if(error)
                return operation_error(
                    destination_relative, "failed to move asset metadata: " + error.message());
            metadata_moved = true;

            auto report = candidate_database.scan();
            const AssetRecord* moved_record = candidate_database.find(handle);
            if(report.snapshot_updated && report.succeeded() && moved_record
                && moved_record->path == destination_relative && moved_record->type == record.type)
                return report;
            if(report.snapshot_updated && report.succeeded())
                report.issues.push_back({.path = destination_relative,
                    .message =
                        "asset database did not resolve the moved identity at its destination"});
            report.snapshot_updated = false;
            return report;
        };

        auto report = execute_move();
        if(report.snapshot_updated && report.succeeded()) {
            database = std::move(candidate_database);
            rollback_on_exit.release();
            return report;
        }

        const bool files_changed = source_moved || metadata_moved;
        rollback();
        rollback_on_exit.release();
        report.snapshot_updated = false;
        report.indexed_assets = database.size();
        report.generated_metadata = 0;
        report.added_assets.clear();
        report.removed_assets.clear();
        report.modified_assets.clear();
        if(metadata_rollback_error || source_rollback_error) {
            std::string message = "asset move failed and file rollback was incomplete";
            if(metadata_rollback_error)
                message += "; metadata: " + metadata_rollback_error.message();
            if(source_rollback_error)
                message += "; source: " + source_rollback_error.message();
            report.issues.push_back({.path = destination_relative, .message = std::move(message)});
        } else if(files_changed) {
            report.issues.push_back({.path = destination_relative,
                .message =
                    "asset move was rolled back because the database snapshot could not be committed"});
        }
        return report;
    }
}
