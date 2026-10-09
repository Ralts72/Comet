#include "assets/source_operations.h"

#include "common/result.h"
#include "common/file_io.h"
#include "common/scope_exit.h"
#include "asset/serialization/metadata_serializer.h"
#include "asset/serialization/material_serializer.h"
#include "asset/import/mesh_importer.h"
#include "asset/import/texture_importer.h"
#include "asset/import/environment_importer.h"
#include "asset/script.h"
#include "audio/audio.h"
#include "diagnostics/logger.h"
#include <fastgltf/core.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <variant>

namespace CometEditor::AssetSourceOperations {
    using Comet::AssetDatabase;
    using Comet::AssetHandle;
    using Comet::AssetImportLimits;
    using Comet::AssetRecord;
    using Comet::AssetScanReport;
    using Comet::AssetType;
    using Comet::AudioClip;
    using Comet::EnvironmentImporter;
    using Comet::MaterialData;
    using Comet::MaterialSerializer;
    using Comet::MeshImporter;
    using Comet::metadata_path;
    using Comet::MetadataSerializer;
    using Comet::ProjectPaths;
    using Comet::Result;
    using Comet::ScopeExit;
    using Comet::Script;
    using Comet::TextureImporter;
    using Comet::write_text_file_atomic;
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

        void restore_staged_file(const std::filesystem::path& staged,
            const std::filesystem::path& original, std::error_code& error) {
            const auto status = std::filesystem::symlink_status(original, error);
            if(error == std::errc::no_such_file_or_directory
                || (!error && !std::filesystem::exists(status))) {
                error.clear();
                std::filesystem::copy_file(
                    staged, original, std::filesystem::copy_options::none, error);
                if(!error)
                    std::filesystem::remove(staged, error);
                return;
            }
            if(error)
                return;
            if(!std::filesystem::is_regular_file(status)
                || !std::filesystem::equivalent(staged, original, error)) {
                if(!error)
                    error = std::make_error_code(std::errc::file_exists);
                return;
            }
            std::filesystem::remove(staged, error);
        }

        std::string extension_of(const std::filesystem::path& path) {
            auto extension = path.extension().string();
            std::ranges::transform(extension, extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }

        bool is_lua_module_source(const std::filesystem::path& path) {
            return extension_of(path) == ".lua" && extension_of(path.stem()) == ".module";
        }

        std::optional<AssetType> external_import_type(const std::filesystem::path& path) {
            const auto extension = extension_of(path);
            if(extension == ".gltf" || extension == ".glb")
                return AssetType::Mesh;
            if(extension == ".png" || extension == ".jpg" || extension == ".jpeg")
                return AssetType::Texture;
            if(extension == ".hdr")
                return AssetType::Environment;
            if(extension == ".lua" && !is_lua_module_source(path))
                return AssetType::Script;
            if(extension == ".wav")
                return AssetType::Audio;
            return std::nullopt;
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

        bool is_case_only_rename(
            const std::filesystem::path& source, const std::filesystem::path& target) {
            if(source == target || source.parent_path() != target.parent_path())
                return false;
            const auto original = source.filename().string();
            const auto renamed = target.filename().string();
            return std::ranges::equal(
                original, renamed, [](unsigned char left, unsigned char right) {
                    return std::tolower(left) == std::tolower(right);
                });
        }

        Result<void> validate_rename_destination(
            const std::filesystem::path& source, const std::filesystem::path& target) {
            if(!is_case_only_rename(source, target))
                return validate_available(target);

            std::error_code error;
            const auto source_status = std::filesystem::symlink_status(source, error);
            if(error || !std::filesystem::is_regular_file(source_status))
                return Result<void>::failure(
                    "Rename source must be a regular file: " + source.string());
            const auto resolved = std::filesystem::weakly_canonical(source, error);
            if(error || resolved != source.lexically_normal())
                return Result<void>::failure(
                    "Rename paths cannot use symlink aliases: " + source.string());

            // equivalent() alone also accepts two separate hard-link directory entries.
            bool found_source = false;
            auto entry = std::filesystem::directory_iterator(source.parent_path(), error);
            const std::filesystem::directory_iterator end;
            while(!error && entry != end) {
                const auto name = entry->path().filename();
                if(name == target.filename())
                    return Result<void>::failure(
                        "Destination already exists (not overwritten): " + target.string());
                found_source = found_source || name == source.filename();
                entry.increment(error);
            }
            if(error)
                return Result<void>::failure("Cannot inspect rename directory: " + error.message());
            if(!found_source)
                return Result<void>::failure(
                    "Rename source spelling does not match its directory entry");

            const auto target_status = std::filesystem::symlink_status(target, error);
            if(error == std::errc::no_such_file_or_directory
                || (!error && !std::filesystem::exists(target_status)))
                return Result<void>::success();
            if(error || !std::filesystem::is_regular_file(target_status)
                || !std::filesystem::equivalent(source, target, error))
                return Result<void>::failure(
                    "Destination already exists (not overwritten): " + target.string());
            return Result<void>::success();
        }

        Result<void> rename_source_file(
            const std::filesystem::path& source, const std::filesystem::path& target) {
            std::error_code error;
            if(!is_case_only_rename(source, target)) {
                std::filesystem::rename(source, target, error);
                if(error)
                    return Result<void>::failure(error.message());
                return Result<void>::success();
            }

            const auto temporary =
                source.parent_path()
                / (".comet-tmp-rename-" + std::to_string(AssetHandle::generate().value()));
            if(auto valid = validate_available(temporary); !valid)
                return valid;
            std::filesystem::rename(source, temporary, error);
            if(error)
                return Result<void>::failure("Cannot stage rename: " + error.message());
            std::filesystem::rename(temporary, target, error);
            if(!error)
                return Result<void>::success();
            std::string message = "Cannot publish rename: " + error.message();
            std::error_code restore_error;
            std::filesystem::rename(temporary, source, restore_error);
            if(restore_error)
                message += "; rollback failed, source retained at '" + temporary.string()
                           + "': " + restore_error.message();
            return Result<void>::failure(std::move(message));
        }

        void restore_moved_module(const std::filesystem::path& target,
            const std::filesystem::path& source, std::error_code& error) {
            const auto status = std::filesystem::symlink_status(source, error);
            if(error == std::errc::no_such_file_or_directory
                || (!error && !std::filesystem::exists(status))) {
                error.clear();
                std::filesystem::create_hard_link(target, source, error);
            } else if(!error
                      && (!std::filesystem::is_regular_file(status)
                          || !std::filesystem::equivalent(target, source, error))) {
                if(!error)
                    error = std::make_error_code(std::errc::file_exists);
            }
            if(!error)
                std::filesystem::remove(target, error);
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

    struct PreparedFileImport::State {
        State(ProjectPaths paths, std::vector<std::filesystem::path> sources,
            std::filesystem::path directory, const AssetImportLimits limits)
            : paths(std::move(paths)), sources(std::move(sources)), directory(std::move(directory)),
              limits(limits) {}

        ~State() { cleanup(nullptr); }

        ProjectPaths paths;
        std::vector<std::filesystem::path> sources;
        std::filesystem::path directory;
        AssetImportLimits limits;
        std::uintmax_t source_bytes = 0;
        std::filesystem::path root;
        std::filesystem::path destination;
        std::filesystem::path resolved_destination;
        std::map<std::filesystem::path, std::filesystem::path> files;
        std::map<std::filesystem::path, std::uintmax_t> file_sizes;
        std::vector<std::filesystem::path> roots;
        std::filesystem::path staging;
        std::vector<std::filesystem::path> published;
        std::vector<std::filesystem::path> created_directories;
        bool scanned = false;
        AssetScanReport report;
        bool committed = false;

        void cleanup(AssetScanReport* diagnostics) {
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
            published.clear();
            created_directories.clear();
            staging.clear();
        }

        Result<void> prepare_destination() {
            std::error_code error;
            if(auto result = validate_relative(directory); !result)
                return result;
            root = std::filesystem::canonical(paths.assets(), error);
            if(error)
                return Result<void>::failure("Cannot resolve assets directory: " + error.message());
            destination = root / directory;
            if(auto result = validate_inside(root, destination); !result)
                return result;
            if(!std::filesystem::is_directory(destination, error))
                return Result<void>::failure("Drop destination is not an existing directory"
                                             + (error ? ": " + error.message() : std::string{}));
            resolved_destination = std::filesystem::canonical(destination, error);
            if(error)
                return Result<void>::failure("Cannot resolve drop destination: " + error.message());
            return Result<void>::success();
        }

        Result<void> recheck_destination() const {
            std::error_code error;
            const auto current_root = std::filesystem::canonical(paths.assets(), error);
            if(error || current_root != root)
                return Result<void>::failure("Project assets directory changed during import");
            const auto current_destination = std::filesystem::canonical(destination, error);
            if(error || current_destination != resolved_destination)
                return Result<void>::failure("Drop destination changed during import");
            return Result<void>::success();
        }

        Result<void> add_file(
            const std::filesystem::path& source, const std::filesystem::path& relative) {
            if(auto result = validate_relative(relative); !result)
                return result;
            std::error_code error;
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
            if(!inserted)
                return Result<void>::success();
            const auto size = std::filesystem::file_size(canonical, error);
            if(error)
                return Result<void>::failure(
                    "Cannot measure import file '" + source.string() + "': " + error.message());
            if(size > limits.external_file_bytes - source_bytes)
                return Result<void>::failure("Import source batch exceeds byte budget of "
                                             + std::to_string(limits.external_file_bytes)
                                             + " bytes");
            source_bytes += size;
            file_sizes.emplace(relative, size);
            return Result<void>::success();
        }

        Result<void> add_model_dependencies(const std::filesystem::path& source) {
            std::error_code error;
            const auto parent = std::filesystem::canonical(source.parent_path(), error);
            if(error)
                return Result<void>::failure("Cannot resolve model directory: " + error.message());
            const auto dependencies = gltf_dependencies(source);
            if(!dependencies)
                return Result<void>::failure(dependencies.error());
            for(const auto& dependency : dependencies.value()) {
                const auto path = source.parent_path() / dependency;
                if(auto result = validate_inside(parent, path); !result)
                    return result;
                if(auto result = add_file(path, dependency); !result)
                    return result;
            }
            return Result<void>::success();
        }

        Result<void> collect_files() {
            std::error_code error;
            // key 是目标相对路径，value 是外部源；相同依赖只复制一次。
            for(const auto& source : sources) {
                if(is_lua_module_source(source))
                    return Result<void>::failure(
                        "Lua module sources (.module.lua) are source-only dependencies, not Script assets; "
                        "create them inside project assets with a source editor");
                const auto type = external_import_type(source);
                if(!type)
                    continue;
                if(!source.is_absolute())
                    return Result<void>::failure("Dropped file path must be absolute");
                const auto relative = source.filename();
                if(auto result = add_file(source, relative); !result)
                    return result;
                if(std::ranges::find(roots, relative) == roots.end())
                    roots.push_back(relative);
                if(*type != AssetType::Mesh)
                    continue;
                if(auto result = add_model_dependencies(source); !result)
                    return result;
            }
            if(roots.empty())
                return Result<void>::failure(
                    "Drop PNG/JPEG textures, HDR environments, Lua scripts, WAV audio or glTF/GLB models (not directories)");
            for(const auto& source : sources) {
                if(external_import_type(source))
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
            return Result<void>::success();
        }

        Result<void> validate_staged_source(const std::filesystem::path& relative) const {
            const auto type = external_import_type(relative);
            if(!type)
                return Result<void>::failure("Unsupported import source: " + relative.string());
            const auto source = staging / relative;
            switch(*type) {
                case AssetType::Mesh: {
                    // 再检查暂存副本，拒绝复制期间改变了依赖列表的源文件。
                    const auto dependencies = gltf_dependencies(source);
                    if(!dependencies)
                        return Result<void>::failure(dependencies.error());
                    for(const auto& dependency : dependencies.value()) {
                        if(!files.contains(dependency))
                            return Result<void>::failure(
                                "glTF dependencies changed during copy; retry import");
                    }
                    if(auto result =
                            MeshImporter{}.import(source, limits.mesh_working_bytes, limits);
                        !result)
                        return Result<void>::failure(result.error());
                    break;
                }
                case AssetType::Texture:
                    if(auto result = TextureImporter{}.import(
                           source, {}, limits.texture_working_bytes, limits);
                        !result)
                        return Result<void>::failure(result.error());
                    break;
                case AssetType::Environment:
                    return EnvironmentImporter{}.validate_source(source);
                case AssetType::Script:
                    if(auto script = Script::load(source); !script)
                        return Result<void>::failure(
                            "Cannot validate standalone Lua import: " + script.error().message
                            + "; scripts requiring project modules must be created inside project assets");
                    break;
                case AssetType::Audio:
                    if(auto clip = AudioClip::load(source); !clip)
                        return Result<void>::failure(clip.error().message);
                    break;
                default:
                    return Result<void>::failure("Unsupported import source: " + relative.string());
            }
            return Result<void>::success();
        }

        Result<void> stage_files() {
            std::error_code error;
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
                const auto expected_size = file_sizes.at(relative);
                const auto current_size = std::filesystem::file_size(source, error);
                if(error || current_size != expected_size)
                    return Result<void>::failure(
                        "Import source size changed during preparation: " + source.string());
                std::filesystem::create_directories(target.parent_path(), error);
                if(error)
                    return Result<void>::failure(
                        "Cannot create staging subdirectory: " + error.message());
                std::filesystem::copy_file(source, target, error);
                if(error)
                    return Result<void>::failure(
                        "Cannot copy import file '" + source.string() + "': " + error.message());
                const auto staged_size = std::filesystem::file_size(target, error);
                if(error || staged_size != expected_size)
                    return Result<void>::failure(
                        "Import source size changed during copy: " + source.string());
            }
            for(const auto& relative : roots)
                if(auto result = validate_staged_source(relative); !result)
                    return result;
            return Result<void>::success();
        }

        Result<void> publish_files(AssetDatabase& database) {
            std::error_code error;
            const auto database_root = std::filesystem::canonical(database.paths().assets(), error);
            if(error || database_root != root)
                return Result<void>::failure("Prepared file import targets a different project");
            if(auto result = recheck_destination(); !result)
                return result;
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
        }

        Result<void> prepare() {
            if(auto prepared = prepare_destination(); !prepared)
                return prepared;
            if(auto collected = collect_files(); !collected)
                return collected;
            return stage_files();
        }

        AssetScanReport publish(AssetDatabase& database) {
            ScopeExit cleanup_on_exit([this] { cleanup(nullptr); });
            const auto result = publish_files(database);
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
    };

    PreparedFileImport::PreparedFileImport(std::unique_ptr<State> state)
        : m_state(std::move(state)) {}
    PreparedFileImport::PreparedFileImport(PreparedFileImport&&) noexcept = default;
    PreparedFileImport& PreparedFileImport::operator=(PreparedFileImport&&) noexcept = default;
    PreparedFileImport::~PreparedFileImport() = default;

    Result<PreparedFileImport> PreparedFileImport::prepare(ProjectPaths paths,
        std::vector<std::filesystem::path> sources, std::filesystem::path directory,
        const AssetImportLimits limits) {
        if(limits.external_file_bytes == 0)
            return Result<PreparedFileImport>::failure("File import byte budget must be positive");
        auto state = std::make_unique<State>(
            std::move(paths), std::move(sources), std::move(directory), limits);
        if(auto result = state->prepare(); !result)
            return Result<PreparedFileImport>::failure(result.error());
        return Result<PreparedFileImport>::success(PreparedFileImport(std::move(state)));
    }

    AssetScanReport PreparedFileImport::publish(AssetDatabase& database) && {
        auto state = std::move(m_state);
        if(!state)
            return operation_error({}, "File import preparation was already consumed");
        return state->publish(database);
    }

    AssetScanReport import_files(AssetDatabase& database,
        const std::span<const std::filesystem::path> sources,
        const std::filesystem::path& directory, const AssetImportLimits limits) {
        auto prepared = PreparedFileImport::prepare(
            database.paths(), {sources.begin(), sources.end()}, directory, limits);
        if(!prepared) {
            auto report = operation_error(directory, prepared.error());
            report.indexed_assets = database.size();
            return report;
        }
        return std::move(prepared).value().publish(database);
    }

    namespace {
        struct TextAssetTransaction {
            AssetDatabase& database;
            ProjectPaths paths;
            const std::filesystem::path& destination;
            std::string_view contents;
            std::optional<AssetType> type;
            std::string_view extension;
            std::string_view name;
            std::filesystem::path target;
            std::filesystem::path meta;
            std::filesystem::path staging;
            AssetHandle handle;
            std::string staging_name;
            AssetScanReport report;
            bool source_published = false;
            bool metadata_published = false;
            bool committed = false;

            void cleanup() {
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
            }

            Result<void> publish() {
                std::error_code error;
                const auto staged_source = staging / ("asset" + std::string(extension));
                if(auto saved = write_text_file_atomic(staged_source, contents); !saved)
                    return saved;
                // 只发布本次创建的文件，不覆盖并发创建的目标。
                if(type) {
                    const auto staged_metadata = metadata_path(staged_source);
                    if(auto saved = MetadataSerializer{}.save(
                           {.handle = handle,
                               .type = *type,
                               .import_settings = make_default_import_settings(*type)},
                           staged_metadata);
                        !saved)
                        return saved;
                    std::filesystem::create_hard_link(staged_metadata, meta, error);
                    if(error)
                        return Result<void>::failure(
                            "Cannot publish " + staging_name + " metadata: " + error.message());
                    metadata_published = true;
                }
                std::filesystem::create_hard_link(staged_source, target, error);
                if(error)
                    return Result<void>::failure(
                        "Cannot publish " + staging_name + ": " + error.message());
                source_published = true;
                AssetDatabase candidate = database;
                report = candidate.scan();
                if(!report.snapshot_updated || !report.succeeded())
                    return Result<void>::failure(
                        std::string(name) + " scan failed; creation rolled back");
                if(type) {
                    const auto* record = candidate.find(handle);
                    if(!record || record->path != destination.lexically_normal()
                        || record->type != *type)
                        return Result<void>::failure(
                            std::string(name) + " could not be indexed; creation rolled back");
                } else if(candidate.find(destination)) {
                    return Result<void>::failure(
                        std::string(name)
                        + " source was unexpectedly indexed; creation rolled back");
                }
                database = std::move(candidate);
                committed = true;
                return Result<void>::success();
            }

            AssetScanReport run() {
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
                target = root / destination;
                meta = metadata_path(target);
                if(auto valid = validate_inside(root, target); !valid)
                    return operation_error(destination, valid.error());
                if(!type) {
                    const auto resolved = std::filesystem::weakly_canonical(target, error);
                    if(error)
                        return operation_error(
                            destination, "Cannot resolve module destination: " + error.message());
                    if(resolved != target.lexically_normal())
                        return operation_error(
                            destination, "Lua module destinations cannot use symlink aliases");
                }
                if(!std::filesystem::is_directory(target.parent_path(), error))
                    return operation_error(
                        destination, std::string(name) + " directory does not exist");
                for(const auto& path : {target, meta})
                    if(auto valid = validate_available(path); !valid)
                        return operation_error(destination, valid.error());

                const auto reservation = AssetHandle::generate();
                if(type)
                    handle = reservation;
                staging_name = name;
                std::ranges::transform(
                    staging_name, staging_name.begin(), [](unsigned char character) {
                        return static_cast<char>(std::tolower(character));
                    });
                const auto staging_parent = paths.cache() / (staging_name + "-create");
                std::filesystem::create_directories(staging_parent, error);
                if(error)
                    return operation_error(
                        destination, "Cannot create staging directory: " + error.message());
                staging = staging_parent / std::to_string(reservation.value());
                if(!std::filesystem::create_directory(staging, error))
                    return operation_error(
                        destination, "Cannot reserve " + staging_name + " staging directory");

                ScopeExit cleanup_on_exit([this] { cleanup(); });
                if(auto published = publish(); !published) {
                    report = operation_error(destination, published.error());
                    report.indexed_assets = database.size();
                }
                cleanup();
                cleanup_on_exit.release();
                return report;
            }
        };

        AssetScanReport create_text_asset(AssetDatabase& database,
            const std::filesystem::path& destination, const std::string_view contents,
            const std::optional<AssetType> type, const std::string_view extension,
            const std::string_view name) {
            return TextAssetTransaction{
                database, database.paths(), destination, contents, type, extension, name}
                .run();
        }
    }

    AssetScanReport create_material(AssetDatabase& database,
        const std::filesystem::path& destination, const MaterialData& data) {
        auto serialized = MaterialSerializer{}.serialize(data);
        if(!serialized)
            return operation_error(destination, serialized.error());
        return create_text_asset(
            database, destination, serialized.value(), AssetType::Material, ".mat", "Material");
    }

    AssetScanReport create_script(
        AssetDatabase& database, const std::filesystem::path& destination, const ScriptKind kind) {
        if(kind == ScriptKind::Module) {
            if(auto valid = Script::module_name(destination); !valid)
                return operation_error(destination, valid.error());
            constexpr std::string_view source = "local module = {}\n\nreturn module\n";
            return create_text_asset(database, destination, source, std::nullopt, ".lua", "Module");
        }
        if(is_lua_module_source(destination))
            return operation_error(destination,
                "New Script cannot create a source-only Lua module (.module.lua); "
                "use New Lua Module instead");
        constexpr std::string_view source = R"(local script = {}

function script:update(dt)
end

return script
)";
        if(auto valid = Script::create(std::string(source), destination.generic_string()); !valid)
            return operation_error(destination, valid.error().message);
        return create_text_asset(
            database, destination, source, AssetType::Script, ".lua", "Script");
    }

    bool can_open_source(const AssetDatabase& database, const std::filesystem::path& source) {
        const auto extension = extension_of(source);
        if(is_lua_module_source(source) || extension == ".glsl")
            return true;
        const auto* record = database.find(source);
        if(!record)
            return false;
        if(extension == ".lua")
            return record->type == AssetType::Script;
        if(extension == ".shader")
            return record->type == AssetType::ShaderProgram;
        return record->type == AssetType::Shader
               && (extension == ".vert" || extension == ".frag" || extension == ".comp"
                   || extension == ".geom");
    }

    Result<std::filesystem::path> resolve_source_file(
        const AssetDatabase& database, const std::filesystem::path& source) {
        using Resolved = Result<std::filesystem::path>;
        if(auto valid = validate_relative(source); !valid)
            return Resolved::failure(valid.error());
        if(!can_open_source(database, source))
            return Resolved::failure(
                "Source is unsupported or no longer present in the asset database: "
                + source.string());
        auto resolved = database.paths().resolve_asset_path(source);
        if(!resolved)
            return resolved;
        std::error_code error;
        if(!std::filesystem::is_regular_file(resolved.value(), error) || error)
            return Resolved::failure("Source is not a regular file: " + source.string());
        return resolved;
    }

    AssetScanReport move_module(AssetDatabase& database, const std::filesystem::path& source,
        const std::filesystem::path& destination) {
        for(const auto& path : {source, destination})
            if(auto valid = Script::module_name(path); !valid)
                return operation_error(path, valid.error());
        if(source == destination)
            return operation_error(destination, "Lua module source and destination are identical");
        std::error_code error;
        const auto root = std::filesystem::canonical(database.paths().assets(), error);
        if(error)
            return operation_error(source, "Cannot resolve assets directory: " + error.message());
        const auto original = root / source;
        const auto target = root / destination;
        const bool case_only = is_case_only_rename(original, target);
        for(const auto& path : {original, target}) {
            if(auto valid = validate_inside(root, path); !valid)
                return operation_error(path, valid.error());
            // Canonicalizing the target file can return the source spelling on an insensitive volume.
            const auto checked = path == target && case_only ? path.parent_path() : path;
            const auto resolved = std::filesystem::weakly_canonical(checked, error);
            if(error)
                return operation_error(path, "Cannot resolve Lua module path: " + error.message());
            if(resolved != checked.lexically_normal())
                return operation_error(path, "Lua module paths cannot use symlink aliases");
        }
        const auto status = std::filesystem::symlink_status(original, error);
        if(error || !std::filesystem::is_regular_file(status))
            return operation_error(source, "Lua module source is not a regular file");
        const auto parent_status = std::filesystem::symlink_status(target.parent_path(), error);
        if(error || !std::filesystem::is_directory(parent_status))
            return operation_error(
                destination, "Lua module destination parent must be an existing directory");
        for(const auto& path : {metadata_path(original), metadata_path(target)})
            if(auto valid = validate_available(path); !valid)
                return operation_error(path, valid.error());
        if(auto valid = validate_rename_destination(original, target); !valid)
            return operation_error(destination, valid.error());

        AssetDatabase candidate = database;
        if(case_only) {
            if(auto moved = rename_source_file(original, target); !moved)
                return operation_error(destination, "Cannot rename Lua module: " + moved.error());
        } else {
            std::filesystem::create_hard_link(original, target, error);
            if(error)
                return operation_error(
                    destination, "Cannot publish moved Lua module: " + error.message());
        }
        std::string rollback_error;
        const auto rollback = [&] {
            if(case_only) {
                if(auto restored = rename_source_file(target, original); !restored)
                    rollback_error = restored.error();
            } else {
                std::error_code restore_error;
                restore_moved_module(target, original, restore_error);
                if(restore_error)
                    rollback_error = restore_error.message();
            }
        };
        ScopeExit rollback_on_exit(rollback);
        AssetScanReport report;
        if(!case_only && !std::filesystem::remove(original, error)) {
            std::string message = "Cannot remove original Lua module";
            if(error)
                message += ": " + error.message();
            report = operation_error(source, std::move(message));
        } else {
            report = candidate.scan();
            if(report.snapshot_updated && report.succeeded()
                && (candidate.find(source) || candidate.find(destination)))
                report.issues.push_back({destination, "Moved Lua module was unexpectedly indexed"});
        }
        if(report.snapshot_updated && report.succeeded()) {
            database = std::move(candidate);
            rollback_on_exit.release();
            return report;
        }

        rollback();
        rollback_on_exit.release();
        report.snapshot_updated = false;
        report.indexed_assets = database.size();
        report.generated_metadata = 0;
        report.added_assets.clear();
        report.removed_assets.clear();
        report.modified_assets.clear();
        if(!rollback_error.empty())
            report.issues.push_back(
                {destination, "Lua module move failed and rollback was incomplete; files retained: "
                                  + rollback_error});
        else
            report.issues.push_back({destination,
                "Lua module move was rolled back because the database snapshot could not be committed"});
        return report;
    }

    namespace {
        struct StagedDeletionFile {
            std::filesystem::path source;
            std::filesystem::path staged;
            bool moved = false;
            bool trashed = false;
            std::error_code restore_error;
        };

        Result<void> stage_deletion_files(const std::span<StagedDeletionFile> files) {
            std::error_code error;
            for(auto& file : files) {
                std::filesystem::create_directories(file.staged.parent_path(), error);
                if(error)
                    return Result<void>::failure(
                        "Cannot prepare deletion staging: " + error.message());
                std::filesystem::rename(file.source, file.staged, error);
                if(error)
                    return Result<void>::failure(
                        "Cannot stage source for deletion: " + error.message());
                file.moved = true;
            }
            return Result<void>::success();
        }

        Result<void> prepare_deletion_trash(const std::span<const StagedDeletionFile> files) {
            std::error_code error;
            for(const auto& file : files) {
                std::filesystem::create_hard_link(file.staged, file.source, error);
                if(error)
                    return Result<void>::failure(
                        "Cannot prepare source for system trash: " + error.message());
            }
            return Result<void>::success();
        }

        Result<void> move_deletion_to_trash(
            const std::span<StagedDeletionFile> files, const TrashMover& move_to_trash) {
            for(auto& file : files) {
                if(auto trashed = move_to_trash(file.source); !trashed) {
                    std::error_code error;
                    const auto status = std::filesystem::symlink_status(file.source, error);
                    file.trashed =
                        error == std::errc::no_such_file_or_directory
                        || (!error && status.type() == std::filesystem::file_type::not_found);
                    return Result<void>::failure(
                        "Cannot move source to system trash: " + trashed.error());
                }
                file.trashed = true;
            }
            return Result<void>::success();
        }

        Result<void> validate_deleted_sources(const std::span<const StagedDeletionFile> files) {
            for(const auto& file : files) {
                std::error_code error;
                const auto status = std::filesystem::symlink_status(file.source, error);
                if(error == std::errc::no_such_file_or_directory)
                    continue;
                if(error || status.type() != std::filesystem::file_type::not_found)
                    return Result<void>::failure("System trash left the source in the project");
            }
            return Result<void>::success();
        }

        void rollback_deletion_files(const std::span<StagedDeletionFile> files,
            const std::filesystem::path& staging_entry, AssetScanReport& report) {
            bool restored = true;
            for(auto file = files.rbegin(); file != files.rend(); ++file) {
                if(file->moved)
                    restore_staged_file(file->staged, file->source, file->restore_error);
                restored = restored && !file->restore_error;
            }
            if(restored) {
                std::error_code cleanup_error;
                std::filesystem::remove_all(staging_entry, cleanup_error);
                if(cleanup_error)
                    report.issues.push_back({staging_entry,
                        "Cannot clean deletion staging entry: " + cleanup_error.message()});
            }
        }

        AssetScanReport remove_source_files(AssetDatabase& database,
            const std::span<const std::filesystem::path> relative_files,
            const std::optional<AssetHandle> removed_asset, const TrashMover& move_to_trash) {
            const auto& source_path = relative_files.front();
            if(!move_to_trash)
                return operation_error(source_path, "System trash is unavailable");
            const auto& paths = database.paths();
            std::error_code error;
            const auto project_root = std::filesystem::canonical(paths.root(), error);
            if(error)
                return operation_error(
                    source_path, "Cannot resolve project root: " + error.message());
            const auto staging_root = paths.local_data() / "pending-deletions";
            if(auto valid = validate_inside(project_root, staging_root); !valid)
                return operation_error(source_path, valid.error());
            std::filesystem::create_directories(staging_root, error);
            if(error)
                return operation_error(
                    source_path, "Cannot create deletion staging area: " + error.message());
            static std::atomic<std::uint64_t> sequence = 0;
            const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto staging_entry = staging_root
                                       / ("Comet-source-" + std::to_string(timestamp) + "-"
                                           + std::to_string(sequence.fetch_add(1)));
            std::vector<StagedDeletionFile> files;
            for(const auto& relative : relative_files)
                files.push_back({paths.assets() / relative, staging_entry / relative});
            AssetDatabase candidate = database;
            AssetScanReport report;
            if(!std::filesystem::create_directory(staging_entry, error))
                return operation_error(source_path, "Cannot reserve deletion staging entry");
            ScopeExit rollback_on_exit(
                [&] { rollback_deletion_files(files, staging_entry, report); });
            auto result = stage_deletion_files(files);
            if(result) {
                report = candidate.scan();
                if(!report.snapshot_updated || !report.succeeded() || candidate.find(source_path))
                    result = Result<void>::failure("Source removal could not be indexed");
                else if(removed_asset
                        && (candidate.find(*removed_asset)
                            || std::ranges::find(report.removed_assets, *removed_asset)
                                   == report.removed_assets.end()))
                    result = Result<void>::failure("Asset removal could not be indexed");
            }
            if(result)
                result = prepare_deletion_trash(files);
            if(result)
                result = move_deletion_to_trash(files, move_to_trash);
            if(result)
                result = validate_deleted_sources(files);
            if(result) {
                database = std::move(candidate);
                rollback_on_exit.release();
                std::error_code cleanup_error;
                std::filesystem::remove_all(staging_entry, cleanup_error);
                if(cleanup_error)
                    LOG_WARN("Cannot clean source deletion staging entry '{}': {}",
                        staging_entry.string(), cleanup_error.message());
                else if(std::filesystem::is_directory(
                            std::filesystem::symlink_status(staging_root, cleanup_error)))
                    std::filesystem::remove(staging_root, cleanup_error);
                LOG_INFO("Moved source '{}' to system trash", source_path.generic_string());
                return report;
            }
            rollback_deletion_files(files, staging_entry, report);
            rollback_on_exit.release();
            report.snapshot_updated = false;
            report.indexed_assets = database.size();
            report.generated_metadata = 0;
            report.added_assets.clear();
            report.removed_assets.clear();
            report.modified_assets.clear();
            report.issues.push_back({source_path, result.error()});
            if(std::ranges::any_of(files, &StagedDeletionFile::trashed))
                report.issues.push_back({source_path,
                    "System trash may contain a copy of the source from a partial operation"});
            for(const auto& file : files)
                if(file.restore_error)
                    report.issues.push_back({file.staged,
                        "Source rollback was incomplete; inspect the deletion staging entry: "
                            + file.restore_error.message()});
            return report;
        }
    }

    AssetScanReport remove_asset(
        AssetDatabase& database, const AssetHandle handle, const TrashMover& move_to_trash) {
        const ProjectPaths paths = database.paths();
        const auto* indexed = database.find(handle);
        if(!indexed)
            return operation_error({}, "Asset is not indexed");
        const AssetRecord record = *indexed;
        if(!move_to_trash)
            return operation_error(record.path, "System trash is unavailable");
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

        return remove_source_files(
            database, std::array{record.path, metadata_path(record.path)}, handle, move_to_trash);
    }

    AssetScanReport remove_module(AssetDatabase& database, const std::filesystem::path& source,
        const TrashMover& move_to_trash) {
        if(auto valid = Script::module_name(source); !valid)
            return operation_error(source, valid.error());
        std::error_code error;
        const auto root = std::filesystem::canonical(database.paths().assets(), error);
        if(error)
            return operation_error(source, "Cannot resolve assets directory: " + error.message());
        const auto original = root / source;
        if(auto valid = validate_inside(root, original); !valid)
            return operation_error(source, valid.error());
        const auto resolved = std::filesystem::weakly_canonical(original, error);
        if(error)
            return operation_error(source, "Cannot resolve Lua module path: " + error.message());
        if(resolved != original.lexically_normal())
            return operation_error(source, "Lua module paths cannot use symlink aliases");
        const auto status = std::filesystem::symlink_status(original, error);
        if(error || !std::filesystem::is_regular_file(status))
            return operation_error(source, "Lua module source is not a regular file");
        if(auto valid = validate_available(metadata_path(original)); !valid)
            return operation_error(source, valid.error());
        return remove_source_files(database, std::array{source}, std::nullopt, move_to_trash);
    }

    AssetScanReport move(AssetDatabase& database, const AssetHandle handle,
        const std::filesystem::path& destination) {
        const ProjectPaths paths = database.paths();
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
        if(record.type == AssetType::Script && is_lua_module_source(destination))
            return operation_error(destination,
                "Script assets cannot be renamed to source-only Lua modules (.module.lua)");
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

        if(auto valid = validate_rename_destination(source, target); !valid)
            return operation_error(destination_relative, valid.error());
        if(auto valid = validate_rename_destination(source_metadata, target_metadata); !valid)
            return operation_error(metadata_path(destination_relative), valid.error());

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
        std::string source_rollback_error;
        std::string metadata_rollback_error;
        const auto rollback = [&] {
            if(metadata_moved) {
                if(auto restored = rename_source_file(target_metadata, source_metadata); !restored)
                    metadata_rollback_error = restored.error();
                metadata_moved = false;
            }
            if(source_moved) {
                if(auto restored = rename_source_file(target, source); !restored)
                    source_rollback_error = restored.error();
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

            if(auto moved = rename_source_file(source, target); !moved)
                return operation_error(
                    destination_relative, "failed to move asset source: " + moved.error());
            source_moved = true;

            if(auto moved = rename_source_file(source_metadata, target_metadata); !moved)
                return operation_error(
                    destination_relative, "failed to move asset metadata: " + moved.error());
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
        if(!metadata_rollback_error.empty() || !source_rollback_error.empty()) {
            std::string message = "asset move failed and file rollback was incomplete";
            if(!metadata_rollback_error.empty())
                message += "; metadata: " + metadata_rollback_error;
            if(!source_rollback_error.empty())
                message += "; source: " + source_rollback_error;
            report.issues.push_back({.path = destination_relative, .message = std::move(message)});
        } else if(files_changed) {
            report.issues.push_back({.path = destination_relative,
                .message =
                    "asset move was rolled back because the database snapshot could not be committed"});
        }
        return report;
    }
}
