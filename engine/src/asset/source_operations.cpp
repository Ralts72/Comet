#include "asset/source_operations.h"

#include "asset/serialization/metadata_serializer.h"

#include <exception>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

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
            return operation_error(source_relative,
                error ? "failed to access asset source: " + error.message()
                      : "asset source is not a regular file");
        }

        const bool target_exists = std::filesystem::exists(target, error);
        if(error || target_exists) {
            return operation_error(destination_relative,
                error ? "failed to inspect asset destination: " + error.message()
                      : "asset destination already exists");
        }
        const bool target_metadata_exists =
            std::filesystem::exists(target_metadata, error);
        if(error || target_metadata_exists) {
            return operation_error(metadata_path(destination_relative),
                error ? "failed to inspect destination metadata: " + error.message()
                      : "destination metadata already exists");
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
            return operation_error(destination_relative,
                rollback_error
                    ? "failed to move metadata and failed to roll back source: "
                          + move_error + "; rollback: " + rollback_error.message()
                    : "failed to move metadata; source move was rolled back: "
                          + move_error);
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
            report.issues.push_back({.path = destination_relative,
                .message = "asset scan failed after move and file rollback was incomplete"
                           + (metadata_rollback_error
                                   ? "; metadata: " + metadata_rollback_error.message()
                                   : std::string{})
                           + (source_rollback_error
                                   ? "; source: " + source_rollback_error.message()
                                   : std::string{})});
        } else {
            report.issues.push_back({.path = destination_relative,
                .message =
                    "asset move was rolled back because the database snapshot could not be committed"});
        }
        return report;
    }
}
