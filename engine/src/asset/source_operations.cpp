#include "asset/source_operations.h"

#include "asset/serialization/metadata_serializer.h"
#include "asset/import/mesh_importer.h"
#include "asset/import/texture_importer.h"
#include <fastgltf/core.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
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
            report.issues.push_back(
                {.path = std::move(path), .message = std::move(message)});
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

        void remove_created_directories(
            const std::vector<std::filesystem::path>& directories) {
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

        void require_relative(const std::filesystem::path& path) {
            if(path.is_absolute() || path.has_root_name())
                throw std::runtime_error(
                    "Import path must be relative: " + path.string());
            for(const auto& part : path) {
                if(part == ".." || part.string().starts_with(".comet-tmp-"))
                    throw std::runtime_error("Unsupported import path: " + path.string());
            }
        }

        void require_inside(
            const std::filesystem::path& root, const std::filesystem::path& path) {
            const auto relative =
                std::filesystem::weakly_canonical(path).lexically_relative(root);
            if(relative.empty())
                throw std::runtime_error("Cannot resolve import path: " + path.string());
            require_relative(relative);
        }

        void require_available(const std::filesystem::path& path) {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if(error && error != std::errc::no_such_file_or_directory)
                throw std::filesystem::filesystem_error(
                    "Cannot inspect destination", path, error);
            if(std::filesystem::exists(status) || std::filesystem::is_symlink(status))
                throw std::runtime_error(
                    "Destination already exists (not overwritten): " + path.string());
        }

        std::vector<std::filesystem::path> gltf_dependencies(
            const std::filesystem::path& source_path) {
            auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
            if(!source)
                throw std::runtime_error(
                    std::string(fastgltf::getErrorMessage(source.error())));
            fastgltf::Parser parser;
            auto asset = parser.loadGltf(source.get(), source_path.parent_path(),
                fastgltf::Options::None,
                fastgltf::Category::Buffers | fastgltf::Category::Images);
            if(!asset)
                throw std::runtime_error(
                    std::string(fastgltf::getErrorMessage(asset.error())));
            std::vector<std::filesystem::path> dependencies;
            const auto collect = [&](const auto& data) {
                const auto* source_uri = std::get_if<fastgltf::sources::URI>(&data);
                if(!source_uri)
                    return; // 内嵌数据不需要复制独立文件。
                const auto& uri = source_uri->uri;
                if(!uri.isLocalPath() || !uri.scheme().empty() || !uri.query().empty()
                    || !uri.fragment().empty())
                    throw std::runtime_error(
                        "Only relative local glTF dependencies are supported");
                const auto relative = uri.fspath();
                require_relative(relative);
                if(relative.empty() || extension_of(relative) == ".meta")
                    throw std::runtime_error(
                        "Invalid glTF dependency: " + relative.string());
                dependencies.push_back(relative.lexically_normal());
            };
            for(const auto& buffer : asset->buffers)
                collect(buffer.data);
            for(const auto& image : asset->images)
                collect(image.data);
            return dependencies;
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
        try {
            require_relative(directory);
            const auto root = std::filesystem::canonical(paths.assets());
            const auto destination = root / directory;
            require_inside(root, destination);
            if(!std::filesystem::is_directory(destination))
                throw std::runtime_error("Drop destination is not an existing directory");

            // key 是目标相对路径，value 是外部源；相同依赖只复制一次。
            std::map<std::filesystem::path, std::filesystem::path> files;
            std::vector<std::filesystem::path> roots;
            const auto add = [&](const std::filesystem::path& source,
                                 const std::filesystem::path& relative) {
                require_relative(relative);
                if(!std::filesystem::is_regular_file(source))
                    throw std::runtime_error(
                        "Missing or unreadable import file: " + source.string());
                const auto canonical = std::filesystem::canonical(source);
                const auto within_project = canonical.lexically_relative(root);
                if(!within_project.empty() && *within_project.begin() != "..")
                    throw std::runtime_error(
                        "File already belongs to this project: " + source.string());
                const auto [it, inserted] = files.emplace(relative, canonical);
                if(!inserted && it->second != canonical)
                    throw std::runtime_error(
                        "Dropped files have conflicting names: " + relative.string());
            };
            for(const auto& source : sources) {
                if(!is_mesh_file(source) && !is_texture_file(source))
                    continue;
                if(!source.is_absolute())
                    throw std::runtime_error("Dropped file path must be absolute");
                const auto relative = source.filename();
                add(source, relative);
                if(std::ranges::find(roots, relative) == roots.end())
                    roots.push_back(relative);
                if(is_mesh_file(source)) {
                    const auto parent = std::filesystem::canonical(source.parent_path());
                    for(const auto& dependency : gltf_dependencies(source)) {
                        require_inside(parent, source.parent_path() / dependency);
                        add(source.parent_path() / dependency, dependency);
                    }
                }
            }
            if(roots.empty())
                throw std::runtime_error(
                    "Drop PNG/JPEG textures or glTF/GLB models (not directories)");
            for(const auto& source : sources) {
                if(is_mesh_file(source) || is_texture_file(source))
                    continue;
                if(extension_of(source) == ".meta") {
                    auto owner = source;
                    owner.replace_extension();
                    if(std::ranges::find(sources, owner) != sources.end())
                        continue;
                }
                const auto canonical = std::filesystem::canonical(source);
                if(std::ranges::none_of(
                       files, [&](const auto& file) { return file.second == canonical; }))
                    throw std::runtime_error(
                        "Unsupported standalone file: " + source.string());
            }
            for(const auto& [relative, source] : files) {
                require_inside(root, destination / relative);
                require_available(destination / relative);
                require_available(metadata_path(destination / relative));
            }

            const auto staging_parent = paths.cache() / "file-import";
            std::filesystem::create_directories(staging_parent);
            const auto candidate =
                staging_parent / std::to_string(AssetHandle::generate().value());
            if(!std::filesystem::create_directory(candidate))
                throw std::runtime_error("Cannot reserve file import staging directory");
            staging = candidate;
            for(const auto& [relative, source] : files) {
                const auto target = staging / relative;
                std::filesystem::create_directories(target.parent_path());
                std::filesystem::copy_file(source, target);
            }
            for(const auto& relative : roots) {
                if(is_mesh_file(relative)) {
                    // 再检查暂存副本，拒绝复制期间改变了依赖列表的源文件。
                    for(const auto& dependency : gltf_dependencies(staging / relative)) {
                        if(!files.contains(dependency))
                            throw std::runtime_error(
                                "glTF dependencies changed during copy; retry import");
                    }
                    static_cast<void>(MeshImporter{}.import(staging / relative));
                } else {
                    static_cast<void>(TextureImporter{}.import(staging / relative));
                }
            }

            published.reserve(files.size());
            for(const auto& [relative, source] : files) {
                const auto target = destination / relative;
                require_inside(root, target);
                require_available(metadata_path(target));
                std::vector<std::filesystem::path> missing;
                for(auto parent = target.parent_path(); !std::filesystem::exists(parent);
                    parent = parent.parent_path())
                    missing.push_back(parent);
                for(auto it = missing.rbegin(); it != missing.rend(); ++it) {
                    if(std::filesystem::create_directory(*it))
                        created_directories.push_back(*it);
                }
                // 同卷硬链接原子发布单个文件，并且不会覆盖竞态中新出现的目标。
                std::filesystem::create_hard_link(staging / relative, target);
                published.push_back(target);
            }
            AssetDatabase candidate_database = database;
            scanned = true;
            report = candidate_database.scan();
            bool indexed = report.snapshot_updated && report.succeeded();
            for(const auto& relative : roots)
                indexed = indexed && candidate_database.find(directory / relative);
            if(!indexed)
                throw std::runtime_error(
                    "Imported files could not be indexed; import rolled back");
            database = std::move(candidate_database);
        } catch(const std::exception& error) {
            report.snapshot_updated = false;
            report.indexed_assets = database.size();
            report.generated_metadata = 0;
            report.added_assets.clear();
            report.removed_assets.clear();
            report.modified_assets.clear();
            report.issues.push_back({directory, error.what()});
            for(auto it = published.rbegin(); it != published.rend(); ++it) {
                std::error_code cleanup_error;
                if(scanned) {
                    std::filesystem::remove(metadata_path(*it), cleanup_error);
                    if(cleanup_error)
                        report.issues.push_back({metadata_path(*it),
                            "Rollback failed: " + cleanup_error.message()});
                }
                std::filesystem::remove(*it, cleanup_error);
                if(cleanup_error)
                    report.issues.push_back(
                        {*it, "Rollback failed: " + cleanup_error.message()});
            }
            std::ranges::reverse(created_directories);
            remove_created_directories(created_directories);
        }
        if(!staging.empty()) {
            std::error_code error;
            std::filesystem::remove_all(staging, error);
            if(error)
                report.issues.push_back(
                    {staging, "Staging cleanup failed: " + error.message()});
        }
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
            return operation_error(destination,
                "asset handle " + std::to_string(handle.value()) + " is not indexed");
        }
        const AssetRecord record = *indexed_record;
        const std::filesystem::path source_relative = record.path.lexically_normal();
        const std::filesystem::path destination_relative = destination.lexically_normal();
        if(source_relative == destination_relative) {
            return operation_error(
                destination_relative, "asset source and destination paths are identical");
        }
        if(source_relative.extension() != destination_relative.extension()) {
            return operation_error(destination_relative,
                "asset move cannot change the source file extension");
        }

        const std::filesystem::path asset_root = paths.assets();
        const std::filesystem::path source = asset_root / source_relative;
        const std::filesystem::path source_metadata = metadata_path(source);
        const std::filesystem::path target = asset_root / destination_relative;
        const std::filesystem::path target_metadata = metadata_path(target);

        try {
            const AssetMetadata metadata =
                AssetMetadataSerializer{}.load(source_metadata);
            if(metadata.handle != handle || metadata.type != record.type) {
                return operation_error(source_relative,
                    "source metadata does not match the indexed asset identity and type");
            }
        } catch(const std::exception& exception) {
            return operation_error(
                source_relative, "cannot move asset with invalid metadata: "
                                     + std::string(exception.what()));
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
        const bool target_metadata_exists =
            std::filesystem::exists(target_metadata, error);
        if(error || target_metadata_exists) {
            std::string message = "destination metadata already exists";
            if(error) {
                message = "failed to inspect destination metadata: " + error.message();
            }
            return operation_error(
                metadata_path(destination_relative), std::move(message));
        }

        const std::filesystem::path canonical_root =
            std::filesystem::weakly_canonical(asset_root, error);
        if(error) {
            return operation_error(destination_relative,
                "failed to resolve assets directory: " + error.message());
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
            return operation_error(destination_relative,
                "asset destination resolves outside the assets directory");
        }

        AssetDatabase candidate_database = database;
        std::vector<std::filesystem::path> created_directories;
        for(std::filesystem::path directory = target.parent_path();
            directory != asset_root && !directory.empty();
            directory = directory.parent_path()) {
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
        std::filesystem::create_directories(target.parent_path(), error);
        if(error) {
            return operation_error(destination_relative,
                "failed to create destination directory: " + error.message());
        }

        std::filesystem::rename(source, target, error);
        if(error) {
            remove_created_directories(created_directories);
            return operation_error(
                destination_relative, "failed to move asset source: " + error.message());
        }

        std::filesystem::rename(source_metadata, target_metadata, error);
        if(error) {
            const std::string move_error = error.message();
            std::error_code rollback_error;
            std::filesystem::rename(target, source, rollback_error);
            remove_created_directories(created_directories);
            std::string message =
                "failed to move metadata; source move was rolled back: " + move_error;
            if(rollback_error) {
                message = "failed to move metadata and failed to roll back source: "
                          + move_error + "; rollback: " + rollback_error.message();
            }
            return operation_error(destination_relative, std::move(message));
        }

        AssetScanReport report;
        try {
            report = candidate_database.scan();
        } catch(const std::exception& exception) {
            report = operation_error(
                destination_relative, "asset database scan failed after move: "
                                          + std::string(exception.what()));
        }
        const AssetRecord* moved_record = candidate_database.find(handle);
        if(report.snapshot_updated && report.succeeded() && moved_record
            && moved_record->path == destination_relative
            && moved_record->type == record.type) {
            database = std::move(candidate_database);
            return report;
        }
        if(report.snapshot_updated && report.succeeded()) {
            report.issues.push_back({.path = destination_relative,
                .message =
                    "asset database did not resolve the moved identity at its destination"});
        }
        report.snapshot_updated = false;
        report.indexed_assets = database.size();
        report.added_assets.clear();
        report.removed_assets.clear();
        report.modified_assets.clear();

        std::error_code metadata_rollback_error;
        std::filesystem::rename(
            target_metadata, source_metadata, metadata_rollback_error);
        std::error_code source_rollback_error;
        std::filesystem::rename(target, source, source_rollback_error);
        remove_created_directories(created_directories);
        if(metadata_rollback_error || source_rollback_error) {
            std::string message =
                "asset scan failed after move and file rollback was incomplete";
            if(metadata_rollback_error) {
                message += "; metadata: " + metadata_rollback_error.message();
            }
            if(source_rollback_error) {
                message += "; source: " + source_rollback_error.message();
            }
            report.issues.push_back(
                {.path = destination_relative, .message = std::move(message)});
        } else {
            report.issues.push_back({.path = destination_relative,
                .message =
                    "asset move was rolled back because the database snapshot could not be committed"});
        }
        return report;
    }
}
