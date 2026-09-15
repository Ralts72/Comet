#include "asset/asset_manager.h"
#include "asset/import/asset_task_queue.h"
#include "asset/import/import_candidate.h"
#include "common/result.h"
#include "render/resource/texture_data.h"

#include "asset/artifact/mesh_artifact.h"
#include "asset/import/import_service.h"
#include "asset/import/texture_importer.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "asset/source_operations.h"
#include "common/file_io.h"
#include "diagnostics/logger.h"
#include "render/material.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_factory.h"
#include "render/resource/texture.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Comet {
    namespace {
        MeshArtifactCandidate build_mesh_artifact_candidate(const ProjectPaths& paths,
            const AssetHandle handle, const AssetRevision revision,
            const std::filesystem::path& relative_path, AssetManager::MeshImportMode mode) {
            const ImportService imports(paths);
            if(mode == AssetManager::MeshImportMode::IfNeeded) {
                if(auto artifact = imports.find_current_mesh_artifact(handle, relative_path)) {
                    return {.handle = handle,
                        .revision = revision,
                        .relative_path = relative_path,
                        .result = Result<MeshArtifact>::success(std::move(*artifact)),
                        .reused_artifact = true};
                }
            }
            return {.handle = handle,
                .revision = revision,
                .relative_path = relative_path,
                .result = imports.build_mesh_artifact(handle, relative_path)};
        }

        TextureImportCandidate import_texture_candidate(const std::filesystem::path& asset_root,
            const AssetHandle handle, const AssetRevision revision,
            const std::filesystem::path& relative_path, const TextureImportSettings& settings) {
            return {.handle = handle,
                .revision = revision,
                .relative_path = relative_path,
                .result = TextureImporter{}.import(asset_root / relative_path, settings)};
        }

        bool validate_asset_handle(const AssetHandle handle, const std::string_view operation) {
            if(handle) {
                return true;
            }
            LOG_ERROR("Cannot {} with an invalid asset handle", operation);
            return false;
        }

        const AssetRecord* find_asset_record(const AssetDatabase& database,
            const AssetHandle handle, const AssetType expected_type) {
            const AssetRecord* record = database.find(handle);
            if(!record) {
                LOG_ERROR("Asset handle {} is not indexed", handle.value());
                return nullptr;
            }
            if(record->type != expected_type) {
                LOG_ERROR("Asset handle {} has type '{}', expected '{}'", handle.value(),
                    to_string(record->type), to_string(expected_type));
                return nullptr;
            }
            return record;
        }

        template<typename T> struct RuntimeAssetLookup {
            std::shared_ptr<T> asset;
            bool type_conflict = false;
        };

        template<typename T>
        RuntimeAssetLookup<T> find_runtime_asset(
            const AssetRegistry& registry, const AssetHandle handle) {
            if(auto asset = registry.resolve<T>(handle)) {
                return {.asset = std::move(asset)};
            }
            if(registry.contains(handle)) {
                LOG_ERROR("Asset handle {} is already registered with another runtime type",
                    handle.value());
                return {.type_conflict = true};
            }
            return {};
        }

        template<typename T, typename Create>
        Result<std::shared_ptr<T>, Error> load_runtime_asset(const AssetDatabase& database,
            AssetRegistry& registry, const AssetHandle handle, const AssetType type,
            Create&& create) {
            using LoadResult = Result<std::shared_ptr<T>, Error>;
            const auto* record = database.find(handle);
            if(!record || record->type != type)
                return LoadResult::failure({"Asset is not indexed with the expected type: "
                                            + std::to_string(handle.value())});
            if(auto asset = registry.resolve<T>(handle))
                return LoadResult::success(std::move(asset));
            if(registry.contains(handle))
                return LoadResult::failure({"Runtime asset type conflict"});
            const AssetRevision revision = database.get_revision(handle);
            // 创建依赖前复制记录；借用不能跨越可能修改数据库的调用。
            const AssetRecord snapshot = *record;
            auto candidate = create(snapshot);
            if(!candidate)
                return candidate;
            if(!database.is_current(handle, revision))
                return LoadResult::failure({"Asset changed during loading"});
            if(!registry.register_asset(handle, candidate.value()))
                return LoadResult::failure({"Failed to publish runtime asset"});
            return candidate;
        }
    }

    AssetManager::AssetManager(ProjectPaths paths, AssetRegistry& registry,
        RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler)
        : AssetManager(
              std::move(paths), registry, resource_factory, task_scheduler, AsyncLimits{}) {}

    AssetManager::AssetManager(ProjectPaths paths, AssetRegistry& registry,
        RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler,
        const AsyncLimits limits)
        : m_paths(std::move(paths)), m_database(m_paths),
          m_import_service(std::make_unique<ImportService>(m_paths)), m_registry(registry),
          m_resource_factory(resource_factory),
          m_task_queue(std::make_unique<AssetTaskQueue>(m_database, task_scheduler, limits)) {}

    AssetManager::~AssetManager() = default;

    AssetManager::AsyncStatus AssetManager::get_async_status() const {
        return m_task_queue->status();
    }

    AssetScanReport AssetManager::scan() {
        AssetScanReport report = m_database.scan();
        apply_scan_report(report);
        return report;
    }

    AssetScanReport AssetManager::move_asset(
        const AssetHandle handle, const std::filesystem::path& destination) {
        AssetScanReport report =
            AssetSourceOperations::move(m_database, m_paths, handle, destination);
        apply_scan_report(report);
        return report;
    }

    AssetScanReport AssetManager::import_files(const std::span<const std::filesystem::path> sources,
        const std::filesystem::path& directory) {
        auto report = AssetSourceOperations::import_files(m_database, m_paths, sources, directory);
        apply_scan_report(report);
        return report;
    }

    void AssetManager::apply_scan_report(const AssetScanReport& report) {
        if(!report.snapshot_updated) {
            return;
        }

        std::unordered_set<AssetHandle> invalidated(
            report.removed_assets.begin(), report.removed_assets.end());
        std::queue<AssetHandle> pending_invalidations;
        for(const AssetHandle handle : report.removed_assets) {
            pending_invalidations.push(handle);
        }
        while(!pending_invalidations.empty()) {
            const AssetHandle dependency = pending_invalidations.front();
            pending_invalidations.pop();
            for(const AssetHandle dependent : m_database.get_dependents(dependency)) {
                if(invalidated.insert(dependent).second) {
                    pending_invalidations.push(dependent);
                }
            }
        }
        for(const AssetHandle handle : invalidated) {
            static_cast<void>(m_registry.unregister_asset(handle));
        }

        for(const AssetHandle handle : report.modified_assets) {
            if(invalidated.contains(handle) || !m_registry.contains(handle)) {
                continue;
            }

            const AssetRecord* record = m_database.find(handle);
            if(!record) {
                continue;
            }
            switch(record->type) {
                case AssetType::Texture:
                    if(!schedule_loaded_texture_refresh(*record)) {
                        LOG_ERROR("Failed to schedule refresh for modified texture asset handle {}",
                            handle.value());
                    }
                    break;
                case AssetType::Material:
                    if(!schedule_material_refresh(*record)) {
                        LOG_ERROR(
                            "Failed to schedule refresh for modified material asset handle {}",
                            handle.value());
                    }
                    break;
                case AssetType::Mesh:
                    if(!schedule_mesh_task(*record, MeshImportMode::Force)) {
                        LOG_ERROR("Failed to schedule refresh for modified mesh asset handle {}",
                            handle.value());
                    }
                    break;
                default:
                    static_cast<void>(m_registry.unregister_asset(handle));
                    LOG_WARN(
                        "Unloaded modified asset handle {} because runtime reload is not implemented for type '{}'",
                        handle.value(), to_string(record->type));
                    break;
            }
        }
    }

    Result<void, Error> AssetManager::ensure_loaded(
        const AssetHandle handle, const AssetType expected_type) {
        switch(expected_type) {
            case AssetType::Mesh: {
                auto loaded = load_mesh(handle);
                return loaded ? Result<void, Error>::success()
                              : Result<void, Error>::failure(loaded.error());
            }
            case AssetType::Material: {
                auto loaded = load_material(handle);
                return loaded ? Result<void, Error>::success()
                              : Result<void, Error>::failure(loaded.error());
            }
            case AssetType::Texture: {
                auto loaded = load_texture(handle);
                return loaded ? Result<void, Error>::success()
                              : Result<void, Error>::failure(loaded.error());
            }
            default:
                return Result<void, Error>::failure(
                    {"Runtime loading is not supported for this asset type"});
        }
    }

    Result<std::vector<AssetHandle>, Error> AssetManager::process_completions() {
        return process_completions(CompletionBudget{});
    }

    Result<std::vector<AssetHandle>, Error> AssetManager::process_completions(
        const CompletionBudget budget) {
        std::vector<AssetHandle> published;
        auto completion = m_task_queue->process_completions(budget,
            [&](AssetImportResult& result) { return publish_import_result(result, published); });
        if(!completion)
            return Result<std::vector<AssetHandle>, Error>::failure(completion.error());
        return Result<std::vector<AssetHandle>, Error>::success(std::move(published));
    }

    Result<void, Error> AssetManager::publish_import_result(
        AssetImportResult& result, std::vector<AssetHandle>& published) {
        if(auto* mesh_candidate = std::get_if<MeshArtifactCandidate>(&result.candidate)) {
            auto& candidate = *mesh_candidate;
            if(!candidate.result) {
                LOG_ERROR("Failed to prepare mesh artifact '{}' (handle {}): {}",
                    candidate.relative_path.generic_string(), candidate.handle.value(),
                    candidate.result.error());
                return Result<void, Error>::success();
            }
            auto& artifact = candidate.result.value();
            if(candidate.reused_artifact) {
                record_import_dependencies(candidate.handle, artifact.source_dependencies());
                return Result<void, Error>::success();
            }
            if(auto publication =
                    artifact.publish_atomic(m_import_service->mesh_artifact_path(candidate.handle));
                !publication) {
                LOG_ERROR("Failed to publish mesh artifact '{}' (handle {}): {}",
                    candidate.relative_path.generic_string(), candidate.handle.value(),
                    publication.error());
                return Result<void, Error>::success();
            }
            record_import_dependencies(candidate.handle, artifact.source_dependencies());

            // Artifact 已发布；后续 GPU 创建失败不撤销这个事实。
            published.push_back(candidate.handle);
            if(auto refreshed =
                    refresh_loaded_mesh(candidate.handle, candidate.revision, artifact.data);
                !refreshed) {
                if(refreshed.error().is_device_lost())
                    return Result<void, Error>::failure(refreshed.error().as_error());
                LOG_ERROR("Failed to refresh mesh handle {}: {}", candidate.handle.value(),
                    refreshed.error().message);
                return Result<void, Error>::success();
            }
            LOG_INFO("Imported mesh artifact '{}' (handle {})",
                candidate.relative_path.generic_string(), candidate.handle.value());
            return Result<void, Error>::success();
        }

        if(auto* candidate = std::get_if<MaterialImportCandidate>(&result.candidate)) {
            const auto handle = candidate->record.handle;
            if(!candidate->result) {
                LOG_ERROR("Failed to read modified material handle {}: {}", handle.value(),
                    candidate->result.error());
                return Result<void, Error>::success();
            }
            const auto runtime = find_runtime_asset<Material>(m_registry, handle);
            if(!runtime.asset)
                return Result<void, Error>::success();
            auto material = create_runtime_material(candidate->record, candidate->result.value());
            if(!material) {
                if(is_device_lost(material.error()))
                    return Result<void, Error>::failure(material.error());
                LOG_ERROR(
                    "Failed to refresh material {}: {}", handle.value(), material.error().message);
                return Result<void, Error>::success();
            }
            if(!m_database.is_current(handle, candidate->revision))
                return Result<void, Error>::success();
            if(auto publication =
                    publish_material(handle, candidate->result.value(), material.value(), true);
                !publication) {
                LOG_ERROR("Failed to publish material {}: {}", handle.value(),
                    publication.error().message);
                return Result<void, Error>::success();
            }
            published.push_back(handle);
            return Result<void, Error>::success();
        }

        auto& candidate = std::get<TextureImportCandidate>(result.candidate);
        if(!candidate.result) {
            LOG_ERROR("Failed to import modified texture asset '{}' (handle {}): {}",
                candidate.relative_path.generic_string(), candidate.handle.value(),
                candidate.result.error());
            return Result<void, Error>::success();
        }
        auto texture_attempt = m_resource_factory.try_create_texture(candidate.result.value());
        if(!texture_attempt) {
            if(texture_attempt.error().is_device_lost())
                return Result<void, Error>::failure(texture_attempt.error().as_error());
            LOG_ERROR("Failed to create refreshed runtime texture for asset handle {}: {}",
                candidate.handle.value(), texture_attempt.error().message);
            return Result<void, Error>::success();
        }
        auto texture = std::move(texture_attempt).value();
        if(!m_database.is_current(candidate.handle, candidate.revision)) {
            LOG_DEBUG("Discarded stale runtime texture candidate for asset handle {} (revision {})",
                candidate.handle.value(), candidate.revision);
            return Result<void, Error>::success();
        }
        if(!m_registry.replace_asset(candidate.handle, texture)) {
            LOG_ERROR("Failed to publish refreshed runtime texture for asset handle {}",
                candidate.handle.value());
            return Result<void, Error>::success();
        }
        if(auto refreshed = reload_loaded_material_dependents(candidate.handle); !refreshed)
            return refreshed;
        published.push_back(candidate.handle);
        LOG_INFO("Reloaded texture asset '{}' (handle {})",
            candidate.relative_path.generic_string(), candidate.handle.value());
        return Result<void, Error>::success();
    }

    Result<void, Error> AssetManager::import_mesh(const AssetHandle handle) {
        if(!validate_asset_handle(handle, "import a mesh")) {
            return Result<void, Error>::failure({"Invalid mesh handle"});
        }

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Mesh);
        if(!record) {
            return Result<void, Error>::failure({"Mesh asset record not found"});
        }
        const AssetRevision revision = m_database.get_revision(handle);
        const AssetRecord snapshot = *record;

        if(m_task_queue->contains(handle, revision)) {
            LOG_WARN("Mesh import is already running for handle {}", handle.value());
            return Result<void, Error>::failure({"Mesh import is already running"});
        }
        if(auto artifact = m_import_service->find_current_mesh_artifact(handle, snapshot.path)) {
            record_import_dependencies(handle, artifact->source_dependencies());
            LOG_DEBUG("Mesh artifact is current '{}' (handle {})", snapshot.path.generic_string(),
                handle.value());
            return Result<void, Error>::success();
        }

        auto imported = m_import_service->build_mesh_artifact(handle, snapshot.path);
        if(!imported) {
            LOG_ERROR("{}", imported.error());
            return Result<void, Error>::failure({imported.error()});
        }
        auto& artifact = imported.value();
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale mesh import for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<void, Error>::failure({"Mesh import revision is stale"});
        }

        if(auto result = artifact.publish_atomic(m_import_service->mesh_artifact_path(handle));
            !result) {
            LOG_ERROR("Failed to publish mesh artifact '{}' (handle {}): {}",
                snapshot.path.generic_string(), handle.value(), result.error());
            return Result<void, Error>::failure({result.error()});
        }
        record_import_dependencies(handle, artifact.source_dependencies());

        if(auto refreshed = refresh_loaded_mesh(handle, revision, artifact.data); !refreshed)
            return Result<void, Error>::failure(refreshed.error().as_error());

        LOG_INFO("Imported mesh artifact '{}' (handle {})", snapshot.path.generic_string(),
            handle.value());
        return Result<void, Error>::success();
    }

    Result<void, GraphicsError> AssetManager::refresh_loaded_mesh(
        const AssetHandle handle, const AssetRevision revision, const MeshData& data) {
        const auto runtime = find_runtime_asset<Mesh>(m_registry, handle);
        if(runtime.type_conflict)
            return Result<void, GraphicsError>::failure({"Runtime mesh type conflict"});
        if(!runtime.asset)
            return Result<void, GraphicsError>::success();

        auto candidate = m_resource_factory.try_create_mesh(data);
        if(!candidate)
            return Result<void, GraphicsError>::failure(candidate.error());
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale runtime mesh candidate for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<void, GraphicsError>::failure({"Runtime mesh revision is stale"});
        }
        if(!m_registry.replace_asset(handle, candidate.value()))
            return Result<void, GraphicsError>::failure({"Failed to publish runtime mesh"});
        return Result<void, GraphicsError>::success();
    }

    bool AssetManager::import_mesh_async(const AssetHandle handle, const MeshImportMode mode) {
        const auto* record = find_asset_record(m_database, handle, AssetType::Mesh);
        return record && schedule_mesh_task(*record, mode);
    }

    Result<std::shared_ptr<Mesh>, Error> AssetManager::load_mesh(const AssetHandle handle) {
        return load_runtime_asset<Mesh>(m_database, m_registry, handle, AssetType::Mesh,
            [this](const AssetRecord& record) { return create_runtime_mesh(record); });
    }

    Result<std::shared_ptr<Texture>, Error> AssetManager::load_texture(const AssetHandle handle) {
        return load_runtime_asset<Texture>(m_database, m_registry, handle, AssetType::Texture,
            [this](const AssetRecord& record) -> Result<std::shared_ptr<Texture>, Error> {
                const auto* settings = std::get_if<TextureImportSettings>(&record.import_settings);
                if(!settings) {
                    return Result<std::shared_ptr<Texture>, Error>::failure(
                        {"Texture asset has incompatible import settings"});
                }
                return create_runtime_texture(record, *settings);
            });
    }

    Result<std::shared_ptr<Texture>, Error> AssetManager::reimport_texture(
        const AssetHandle handle, TextureImportSettings import_settings) {
        if(!validate_asset_handle(handle, "reimport a texture")) {
            return Result<std::shared_ptr<Texture>, Error>::failure({"Invalid texture handle"});
        }

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Texture);
        if(!record) {
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Texture is not indexed with the expected type"});
        }

        const auto runtime = find_runtime_asset<Texture>(m_registry, handle);
        if(runtime.type_conflict) {
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Runtime texture type conflict"});
        }
        const auto& previous_texture = runtime.asset;

        const AssetRevision revision = m_database.get_revision(handle);
        const AssetRecord snapshot = *record;
        auto texture = create_runtime_texture(snapshot, import_settings);
        if(!texture)
            return texture;
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale texture reimport for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Texture changed during reimport"});
        }

        if(auto updated = m_database.update_import_settings(handle, import_settings); !updated) {
            return Result<std::shared_ptr<Texture>, Error>::failure({updated.error()});
        }

        const bool published = previous_texture
                                   ? m_registry.replace_asset(handle, texture.value())
                                   : m_registry.register_asset(handle, texture.value());
        if(!published) {
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Failed to publish runtime texture"});
        }

        if(auto refreshed = reload_loaded_material_dependents(handle); !refreshed)
            return Result<std::shared_ptr<Texture>, Error>::failure(refreshed.error());
        LOG_INFO("Reimported texture asset '{}' (handle {}, color_space={}, flip_y={})",
            snapshot.path.generic_string(), handle.value(), to_string(import_settings.color_space),
            import_settings.flip_y);
        return texture;
    }

    Result<void, Error> AssetManager::reload_loaded_material_dependents(
        const AssetHandle texture_handle) {
        const auto dependents = m_database.get_dependents(texture_handle);
        // 重载会修改依赖索引，先复制句柄。
        const std::vector<AssetHandle> snapshot(dependents.begin(), dependents.end());
        for(const auto handle : snapshot) {
            const auto* record = m_database.find(handle);
            if(!record || record->type != AssetType::Material
                || !m_registry.resolve<Material>(handle))
                continue;
            if(auto refreshed = reload_material(handle); !refreshed) {
                if(is_device_lost(refreshed.error()))
                    return Result<void, Error>::failure(refreshed.error());
                LOG_ERROR("Texture {} updated, but dependent material {} failed: {}",
                    texture_handle.value(), handle.value(), refreshed.error().message);
            }
        }
        return Result<void, Error>::success();
    }

    Result<std::shared_ptr<Material>, Error> AssetManager::load_material(const AssetHandle handle) {
        return load_runtime_asset<Material>(m_database, m_registry, handle, AssetType::Material,
            [this](const AssetRecord& record) { return create_runtime_material(record); });
    }

    Result<std::shared_ptr<Material>, Error> AssetManager::reload_material(
        const AssetHandle handle) {
        if(!validate_asset_handle(handle, "reload a material")) {
            return Result<std::shared_ptr<Material>, Error>::failure({"Invalid material handle"});
        }

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Material);
        if(!record) {
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Material is not indexed with the expected type"});
        }

        const auto runtime = find_runtime_asset<Material>(m_registry, handle);
        if(runtime.type_conflict) {
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Runtime material type conflict"});
        }
        const bool has_runtime_asset = static_cast<bool>(runtime.asset);

        const AssetRevision revision = m_database.get_revision(handle);
        const AssetRecord snapshot = *record;
        const auto data = MaterialSerializer{}.load(m_paths.assets() / snapshot.path);
        if(!data) {
            return Result<std::shared_ptr<Material>, Error>::failure({data.error()});
        }
        auto material = create_runtime_material(snapshot, data.value());
        if(!material)
            return material;
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale material reload for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Material changed during reload"});
        }
        if(auto published =
                publish_material(handle, data.value(), material.value(), has_runtime_asset);
            !published)
            return Result<std::shared_ptr<Material>, Error>::failure(published.error());
        LOG_INFO("Reloaded material asset '{}' (handle {})", snapshot.path.generic_string(),
            handle.value());
        return material;
    }

    Result<std::shared_ptr<Material>, Error> AssetManager::update_material(
        const AssetHandle handle, const MaterialData& data) {
        if(!validate_asset_handle(handle, "update a material")) {
            return Result<std::shared_ptr<Material>, Error>::failure({"Invalid material handle"});
        }

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Material);
        if(!record) {
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Material is not indexed with the expected type"});
        }

        const auto runtime = find_runtime_asset<Material>(m_registry, handle);
        if(runtime.type_conflict) {
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Runtime material type conflict"});
        }
        const bool has_runtime_asset = static_cast<bool>(runtime.asset);

        const AssetRevision revision = m_database.get_revision(handle);
        const AssetRecord snapshot = *record;
        const auto serialized_data = MaterialSerializer{}.serialize(data);
        if(!serialized_data) {
            return Result<std::shared_ptr<Material>, Error>::failure({serialized_data.error()});
        }
        auto material = create_runtime_material(snapshot, data);
        if(!material)
            return material;
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale material update for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Material changed during update"});
        }
        if(auto saved =
                write_text_file_atomic(m_paths.assets() / snapshot.path, serialized_data.value());
            !saved) {
            return Result<std::shared_ptr<Material>, Error>::failure({saved.error()});
        }
        if(auto published = publish_material(handle, data, material.value(), has_runtime_asset);
            !published)
            return Result<std::shared_ptr<Material>, Error>::failure(published.error());
        LOG_INFO("Updated material asset '{}' (handle {})", snapshot.path.generic_string(),
            handle.value());
        return material;
    }

    Result<void, Error> AssetManager::publish_material(const AssetHandle handle,
        const MaterialData& data, const std::shared_ptr<Material>& material,
        const bool replace_existing) {
        if(auto updated = m_database.update_dependencies(handle, get_asset_dependencies(data));
            !updated)
            return Result<void, Error>::failure({updated.error()});
        const bool published = replace_existing ? m_registry.replace_asset(handle, material)
                                                : m_registry.register_asset(handle, material);
        if(!published)
            return Result<void, Error>::failure({"Failed to publish material"});
        return Result<void, Error>::success();
    }

    Result<std::shared_ptr<Mesh>, Error> AssetManager::create_runtime_mesh(
        const AssetRecord& record) {
        const auto handle = record.handle;
        const auto artifact =
            MeshArtifact::load(m_import_service->mesh_artifact_path(handle), handle);
        if(!artifact)
            return Result<std::shared_ptr<Mesh>, Error>::failure(
                {"Mesh artifact is missing or invalid; import the asset before loading: "
                    + record.path.generic_string()});
        record_import_dependencies(handle, artifact->source_dependencies());
        auto mesh = m_resource_factory.try_create_mesh(artifact->data);
        if(!mesh)
            return Result<std::shared_ptr<Mesh>, Error>::failure(mesh.error().as_error());
        return Result<std::shared_ptr<Mesh>, Error>::success(std::move(mesh).value());
    }

    void AssetManager::record_import_dependencies(
        const AssetHandle handle, const std::vector<std::filesystem::path>& dependencies) {
        if(auto updated = m_database.update_import_dependencies(handle, dependencies); !updated) {
            LOG_WARN("Could not index import dependencies for asset handle {}: {}", handle.value(),
                updated.error());
        }
    }

    bool AssetManager::schedule_mesh_task(const AssetRecord& record, const MeshImportMode mode) {
        const auto handle = record.handle;
        const auto revision = m_database.get_revision(handle);
        return m_task_queue->schedule(
            handle, revision,
            [paths = m_paths, handle, revision, relative_path = record.path, mode](
                AssetImportResult& result) {
                result.candidate =
                    build_mesh_artifact_candidate(paths, handle, revision, relative_path, mode);
            },
            mode == MeshImportMode::Force);
    }

    bool AssetManager::schedule_material_refresh(const AssetRecord& record) {
        const auto revision = m_database.get_revision(record.handle);
        return m_task_queue->schedule(record.handle, revision,
            [record, revision, root = m_paths.assets()](AssetImportResult& result) {
                result.candidate = MaterialImportCandidate{
                    record, revision, MaterialSerializer{}.load(root / record.path)};
            });
    }

    bool AssetManager::schedule_loaded_texture_refresh(const AssetRecord& record) {
        const AssetHandle handle = record.handle;
        const AssetRevision revision = m_database.get_revision(handle);
        const auto previous_texture = m_registry.resolve<Texture>(handle);
        if(!previous_texture) {
            return !m_registry.contains(handle);
        }

        const auto* settings = std::get_if<TextureImportSettings>(&record.import_settings);
        if(!settings) {
            LOG_ERROR("Texture asset handle {} has incompatible import settings", handle.value());
            return false;
        }

        return m_task_queue->schedule(handle, revision,
            [asset_root = m_paths.assets(), handle, revision, relative_path = record.path,
                settings = *settings](AssetImportResult& result) {
                result.candidate =
                    import_texture_candidate(asset_root, handle, revision, relative_path, settings);
            });
    }

    Result<std::shared_ptr<Texture>, Error> AssetManager::create_runtime_texture(
        const AssetRecord& record, const TextureImportSettings& import_settings) {
        auto data = TextureImporter{}.import(m_paths.assets() / record.path, import_settings);
        if(!data)
            return Result<std::shared_ptr<Texture>, Error>::failure({data.error()});
        auto texture = m_resource_factory.try_create_texture(data.value());
        if(!texture)
            return Result<std::shared_ptr<Texture>, Error>::failure(texture.error().as_error());
        return Result<std::shared_ptr<Texture>, Error>::success(std::move(texture).value());
    }

    Result<std::shared_ptr<Material>, Error> AssetManager::create_runtime_material(
        const AssetRecord& record) {
        auto data = MaterialSerializer{}.load(m_paths.assets() / record.path);
        if(!data)
            return Result<std::shared_ptr<Material>, Error>::failure({data.error()});
        return create_runtime_material(record, data.value());
    }

    Result<std::shared_ptr<Material>, Error> AssetManager::create_runtime_material(
        const AssetRecord& record, const MaterialData& data) {
        std::map<std::string, std::shared_ptr<Texture>> textures;
        for(const auto& [property_name, texture_handle] : data.texture_properties) {
            auto texture = load_texture(texture_handle);
            if(!texture) {
                auto error = texture.error();
                error.message = "Material '" + record.path.generic_string() + "' property '"
                                + property_name + "': " + error.message;
                return Result<std::shared_ptr<Material>, Error>::failure(std::move(error));
            }
            textures.emplace(property_name, std::move(texture).value());
        }

        auto material = std::make_shared<Material>(record.path.stem().string(), data.template_name);
        for(const auto& [property_name, texture] : textures) {
            material->set_texture_property(property_name, texture);
        }
        for(const auto& [name, value] : data.scalar_properties) {
            if(!material->set_scalar_property(name, value))
                return Result<std::shared_ptr<Material>, Error>::failure(
                    {"Material scalar '" + name + "' must be finite"});
        }
        for(const auto& [name, value] : data.vector_properties) {
            if(!material->set_vector_property(name, value))
                return Result<std::shared_ptr<Material>, Error>::failure(
                    {"Material vector '" + name + "' must be finite"});
        }
        return Result<std::shared_ptr<Material>, Error>::success(std::move(material));
    }
}
