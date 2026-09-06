#include "asset/asset_manager.h"

#include "asset/artifact/mesh_artifact.h"
#include "asset/import/import_service.h"
#include "asset/import/texture_importer.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "asset/source_operations.h"
#include "common/file_io.h"
#include "core/task_scheduler.h"
#include "diagnostics/logger.h"
#include "render/material.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_factory.h"
#include "render/resource/texture.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <exception>
#include <future>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace Comet {
    namespace {
        struct MeshArtifactCandidate {
            AssetHandle handle;
            AssetRevision revision = INVALID_ASSET_REVISION;
            std::filesystem::path relative_path;
            MeshArtifact artifact;
            std::optional<AssetManager::MeshImportState> inspected_state;
            std::string error;
        };

        struct TextureImportCandidate {
            AssetHandle handle;
            AssetRevision revision = INVALID_ASSET_REVISION;
            std::filesystem::path relative_path;
            TextureData data;
            std::string error;
        };

        MeshArtifactCandidate build_mesh_artifact_candidate(const ProjectPaths& paths,
            const AssetHandle handle, const AssetRevision revision,
            const std::filesystem::path& relative_path, const bool inspect_only = false) {
            MeshArtifactCandidate candidate{
                .handle = handle, .revision = revision, .relative_path = relative_path};

            try {
                const ImportService imports(paths);
                if(inspect_only) {
                    candidate.inspected_state = AssetManager::MeshImportState::Missing;
                    if(std::filesystem::exists(imports.mesh_artifact_path(handle))) {
                        candidate.inspected_state = AssetManager::MeshImportState::Stale;
                        if(auto artifact = imports.find_current_mesh_artifact(
                               handle, relative_path)) {
                            candidate.artifact = std::move(*artifact);
                            candidate.inspected_state =
                                AssetManager::MeshImportState::Ready;
                        }
                    }
                } else {
                    candidate.artifact =
                        imports.build_mesh_artifact(handle, relative_path);
                }
            } catch(const std::exception& exception) {
                candidate.error = exception.what();
            } catch(...) {
                candidate.error = "Unknown mesh import failure";
            }
            return candidate;
        }

        TextureImportCandidate import_texture_candidate(
            const std::filesystem::path& asset_root, const AssetHandle handle,
            const AssetRevision revision, const std::filesystem::path& relative_path,
            const TextureImportSettings& settings) {
            TextureImportCandidate candidate{
                .handle = handle, .revision = revision, .relative_path = relative_path};
            try {
                candidate.data =
                    TextureImporter{}.import(asset_root / relative_path, settings);
            } catch(const std::exception& exception) {
                candidate.error = exception.what();
            } catch(...) {
                candidate.error = "Unknown texture import failure";
            }
            return candidate;
        }

        bool validate_asset_handle(
            const AssetHandle handle, const std::string_view operation) {
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
                LOG_ERROR(
                    "Asset handle {} is already registered with another runtime type",
                    handle.value());
                return {.type_conflict = true};
            }
            return {};
        }

        template<typename T>
        bool publish_runtime_asset(AssetRegistry& registry, const AssetHandle handle,
            const std::shared_ptr<T>& asset, const bool replace_existing,
            const std::string_view asset_type) {
            bool published = false;
            if(replace_existing) {
                published = registry.replace_asset(handle, asset);
            } else {
                published = registry.register_asset(handle, asset);
            }
            if(!published) {
                LOG_ERROR("Failed to publish runtime {} for asset handle {}", asset_type,
                    handle.value());
            }
            return published;
        }
    }

    struct AssetManager::ImportResult {
        std::variant<MeshArtifactCandidate, TextureImportCandidate> candidate;
    };

    struct AssetManager::AsyncState {
        struct MeshStatus {
            AssetRevision revision;
            MeshImportState state;
        };
        struct ScheduledAssetTask {
            AssetHandle handle;
            AssetRevision revision = INVALID_ASSET_REVISION;
            AssetType type = AssetType::Unknown;
            std::future<void> completion;
            std::shared_ptr<ImportResult> result;
        };
        struct QueuedAssetTask {
            AssetHandle handle;
            AssetRevision revision;
            AssetType type;
            std::function<void(ImportResult&)> task;
        };

        std::unordered_map<AssetHandle, AssetRevision> pending_assets;
        std::vector<ScheduledAssetTask> scheduled_tasks;
        std::deque<QueuedAssetTask> queued_tasks;
        std::unordered_map<AssetHandle, MeshStatus> mesh_status;
        bool processing_completions = false;
    };

    AssetManager::AssetManager(ProjectPaths paths, AssetRegistry& registry,
        RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler)
        : AssetManager(std::move(paths), registry, resource_factory, task_scheduler,
              AsyncLimits{}) {}

    AssetManager::AssetManager(ProjectPaths paths, AssetRegistry& registry,
        RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler,
        const AsyncLimits limits)
        : m_paths(std::move(paths)), m_database(m_paths),
          m_import_service(std::make_unique<ImportService>(m_paths)),
          m_registry(registry), m_resource_factory(resource_factory),
          m_task_scheduler(task_scheduler), m_async_limits(limits),
          m_async_state(std::make_unique<AsyncState>()) {
        if(limits.in_flight == 0 || limits.queued == 0)
            throw std::invalid_argument("Asset async limits must be positive");
        m_async_state->scheduled_tasks.reserve(limits.in_flight);
    }

    AssetManager::~AssetManager() {
        // 未交给 Worker 的请求取消；已派发结果只保留到任务完成，不在析构中发布。
        m_async_state->queued_tasks.clear();
        for(AsyncState::ScheduledAssetTask& task : m_async_state->scheduled_tasks) {
            task.completion.wait();
        }
    }

    AssetManager::AsyncStatus AssetManager::get_async_status() const {
        return {.in_flight = m_async_state->scheduled_tasks.size(),
            .queued = m_async_state->queued_tasks.size()};
    }

    AssetScanReport AssetManager::scan() {
        AssetScanReport report = m_database.scan();
        // 显式 Refresh 也会重新检查磁盘缓存；运行中的任务仍由 revision 验票。
        std::erase_if(m_async_state->mesh_status, [this](const auto& entry) {
            return !m_async_state->pending_assets.contains(entry.first);
        });
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
                        LOG_ERROR(
                            "Failed to schedule refresh for modified texture asset handle {}",
                            handle.value());
                    }
                    break;
                case AssetType::Material:
                    if(!reload_material(handle)) {
                        LOG_ERROR("Failed to refresh modified material asset handle {}",
                            handle.value());
                    }
                    break;
                case AssetType::Mesh:
                    if(!schedule_loaded_mesh_refresh(*record)) {
                        LOG_ERROR(
                            "Failed to schedule refresh for modified mesh asset handle {}",
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

    bool AssetManager::ensure_loaded(
        const AssetHandle handle, const AssetType expected_type) {
        if(!validate_asset_handle(handle, "load an asset")
            || !find_asset_record(m_database, handle, expected_type))
            return false;
        try {
            switch(expected_type) {
                case AssetType::Mesh:
                    return static_cast<bool>(load_mesh(handle));
                case AssetType::Material:
                    return static_cast<bool>(load_material(handle));
                case AssetType::Texture:
                    return static_cast<bool>(load_texture(handle));
                default:
                    LOG_WARN("Runtime loading is not implemented for asset type '{}'",
                        to_string(expected_type));
                    return false;
            }
        } catch(const std::exception& error) {
            LOG_ERROR("Cannot load asset {}: {}", handle.value(), error.what());
            return false;
        }
    }

    std::vector<AssetHandle> AssetManager::process_completions() {
        return process_completions(CompletionBudget{});
    }

    std::vector<AssetHandle> AssetManager::process_completions(
        const CompletionBudget budget) {
        if(m_async_state->processing_completions) {
            LOG_WARN("Ignoring reentrant asset completion processing");
            return {};
        }
        struct ProcessingScope {
            bool& active;
            ~ProcessingScope() { active = false; }
        } scope{m_async_state->processing_completions};
        scope.active = true;

        const auto start = std::chrono::steady_clock::now();
        std::vector<AssetHandle> published;
        auto& tasks = m_async_state->scheduled_tasks;
        published.reserve(std::min(budget.max_results, tasks.size()));
        std::size_t processed = 0;
        for(auto task = tasks.begin(); task != tasks.end();) {
            if(processed >= budget.max_results
                || budget.max_time <= std::chrono::nanoseconds::zero()
                || (processed > 0
                    && std::chrono::steady_clock::now() - start >= budget.max_time))
                break;
            if(task->completion.wait_for(std::chrono::seconds(0))
                != std::future_status::ready) {
                ++task;
                continue;
            }
            ++processed;
            const auto pending = m_async_state->pending_assets.find(task->handle);
            if(pending != m_async_state->pending_assets.end()
                && pending->second == task->revision)
                m_async_state->pending_assets.erase(pending);

            const auto report_failure = [&](const std::string_view message) {
                if(task->type == AssetType::Mesh
                    && m_database.is_current(task->handle, task->revision))
                    m_async_state->mesh_status[task->handle] = {
                        task->revision, MeshImportState::Failed};
                LOG_ERROR("Background {} task failed for asset handle {}: {}",
                    to_string(task->type), task->handle.value(), message);
            };

            try {
                // future 完成建立 Worker 写结果到 owner 读取结果的同步关系。
                task->completion.get();
                if(!m_database.is_current(task->handle, task->revision)) {
                    LOG_DEBUG("Discarded stale background asset {} (revision {})",
                        task->handle.value(), task->revision);
                } else if(publish_import_result(*task->result)) {
                    published.push_back(task->handle);
                }
            } catch(const std::exception& error) {
                report_failure(error.what());
            } catch(...) {
                report_failure("Unknown completion failure");
            }
            // 发布／丢弃后才释放结果和在途槽，预算外的完成结果继续占用额度。
            task = tasks.erase(task);
        }
        dispatch_queued_tasks();
        return published;
    }

    bool AssetManager::publish_import_result(ImportResult& result) {
        if(auto* mesh_candidate = std::get_if<MeshArtifactCandidate>(&result.candidate)) {
            auto& candidate = *mesh_candidate;
            if(!candidate.error.empty()) {
                m_async_state->mesh_status[candidate.handle] = {
                    candidate.revision, MeshImportState::Failed};
                LOG_ERROR("Failed to prepare mesh artifact '{}' (handle {}): {}",
                    candidate.relative_path.generic_string(), candidate.handle.value(),
                    candidate.error);
                return false;
            }
            if(candidate.inspected_state) {
                if(*candidate.inspected_state == MeshImportState::Ready)
                    record_import_dependencies(
                        candidate.handle, candidate.artifact.source_dependencies());
                m_async_state->mesh_status[candidate.handle] = {
                    candidate.revision, *candidate.inspected_state};
                return false;
            }
            try {
                candidate.artifact.publish_atomic(
                    m_import_service->mesh_artifact_path(candidate.handle));
                record_import_dependencies(
                    candidate.handle, candidate.artifact.source_dependencies());
            } catch(const std::exception& exception) {
                m_async_state->mesh_status[candidate.handle] = {
                    candidate.revision, MeshImportState::Failed};
                LOG_ERROR("Failed to publish mesh artifact '{}' (handle {}): {}",
                    candidate.relative_path.generic_string(), candidate.handle.value(),
                    exception.what());
                return false;
            }

            m_async_state->mesh_status[candidate.handle] = {
                candidate.revision, MeshImportState::Ready};
            const auto runtime = find_runtime_asset<Mesh>(m_registry, candidate.handle);
            if(runtime.type_conflict)
                return true;
            if(!runtime.asset) {
                LOG_INFO("Imported mesh artifact '{}' (handle {})",
                    candidate.relative_path.generic_string(), candidate.handle.value());
                return true;
            }

            std::shared_ptr<Mesh> mesh;
            try {
                auto mesh_attempt =
                    m_resource_factory.try_create_mesh(candidate.artifact.data);
                if(!mesh_attempt) {
                    LOG_ERROR(
                        "Failed to create refreshed runtime mesh for asset handle {}: {}",
                        candidate.handle.value(), vk::to_string(mesh_attempt.result()));
                    return true;
                }
                mesh = std::move(mesh_attempt).value();
            } catch(const std::exception& exception) {
                LOG_ERROR(
                    "Failed to create refreshed runtime mesh for asset handle {}: {}",
                    candidate.handle.value(), exception.what());
                return true;
            } catch(...) {
                LOG_ERROR("Unknown runtime mesh creation failure for asset handle {}",
                    candidate.handle.value());
                return true;
            }
            if(!m_database.is_current(candidate.handle, candidate.revision)) {
                LOG_DEBUG(
                    "Discarded stale runtime mesh candidate for asset handle {} (revision {})",
                    candidate.handle.value(), candidate.revision);
                return true;
            }
            if(!m_registry.replace_asset(candidate.handle, mesh)) {
                LOG_ERROR("Failed to publish refreshed runtime mesh for asset handle {}",
                    candidate.handle.value());
                return true;
            }

            LOG_INFO("Reloaded mesh asset '{}' (handle {})",
                candidate.relative_path.generic_string(), candidate.handle.value());
            return true;
        }

        auto& candidate = std::get<TextureImportCandidate>(result.candidate);
        if(!candidate.error.empty()) {
            LOG_ERROR("Failed to import modified texture asset '{}' (handle {}): {}",
                candidate.relative_path.generic_string(), candidate.handle.value(),
                candidate.error);
            return false;
        }

        std::shared_ptr<Texture> texture;
        try {
            auto texture_attempt = m_resource_factory.try_create_texture(candidate.data);
            if(!texture_attempt) {
                LOG_ERROR(
                    "Failed to create refreshed runtime texture for asset handle {}: {}",
                    candidate.handle.value(), vk::to_string(texture_attempt.result()));
                return false;
            }
            texture = std::move(texture_attempt).value();
        } catch(const std::exception& exception) {
            LOG_ERROR(
                "Failed to create refreshed runtime texture for asset handle {}: {}",
                candidate.handle.value(), exception.what());
            return false;
        } catch(...) {
            LOG_ERROR("Unknown runtime texture creation failure for asset handle {}",
                candidate.handle.value());
            return false;
        }

        if(!m_database.is_current(candidate.handle, candidate.revision)) {
            LOG_DEBUG(
                "Discarded stale runtime texture candidate for asset handle {} (revision {})",
                candidate.handle.value(), candidate.revision);
            return false;
        }
        if(!m_registry.replace_asset(candidate.handle, texture)) {
            LOG_ERROR("Failed to publish refreshed runtime texture for asset handle {}",
                candidate.handle.value());
            return false;
        }

        reload_loaded_material_dependents(candidate.handle);
        LOG_INFO("Reloaded texture asset '{}' (handle {})",
            candidate.relative_path.generic_string(), candidate.handle.value());
        return true;
    }

    bool AssetManager::import_mesh(const AssetHandle handle) {
        if(!validate_asset_handle(handle, "import a mesh")) {
            return false;
        }

        const AssetRecord* record =
            find_asset_record(m_database, handle, AssetType::Mesh);
        if(!record) {
            return false;
        }
        const AssetRevision revision = m_database.get_revision(handle);

        if(const auto pending = m_async_state->pending_assets.find(handle);
            pending != m_async_state->pending_assets.end()
            && pending->second == revision) {
            LOG_WARN("Mesh import is already running for handle {}", handle.value());
            return false;
        }
        m_async_state->mesh_status[handle] = {revision, MeshImportState::Failed};
        if(auto artifact =
                m_import_service->find_current_mesh_artifact(handle, record->path)) {
            record_import_dependencies(handle, artifact->source_dependencies());
            LOG_DEBUG("Mesh artifact is current '{}' (handle {})",
                record->path.generic_string(), handle.value());
            m_async_state->mesh_status[handle] = {revision, MeshImportState::Ready};
            return true;
        }

        MeshArtifact artifact;
        try {
            artifact = m_import_service->build_mesh_artifact(handle, record->path);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return false;
        }
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale mesh import for asset handle {} (revision {})",
                handle.value(), revision);
            return false;
        }

        try {
            artifact.publish_atomic(m_import_service->mesh_artifact_path(handle));
            record_import_dependencies(handle, artifact.source_dependencies());
        } catch(const std::exception& exception) {
            LOG_ERROR("Failed to publish mesh artifact '{}' (handle {}): {}",
                record->path.generic_string(), handle.value(), exception.what());
            return false;
        }

        m_async_state->mesh_status[handle] = {revision, MeshImportState::Ready};
        const auto runtime = find_runtime_asset<Mesh>(m_registry, handle);
        if(runtime.type_conflict) {
            return false;
        }
        if(runtime.asset) {
            auto mesh_attempt = m_resource_factory.try_create_mesh(artifact.data);
            if(!mesh_attempt) {
                LOG_ERROR("Failed to create reimported runtime mesh for handle {}: {}",
                    handle.value(), vk::to_string(mesh_attempt.result()));
                return false;
            }
            if(!m_database.is_current(handle, revision)
                || !m_registry.replace_asset(handle, std::move(mesh_attempt).value())) {
                return false;
            }
        }

        LOG_INFO("Imported mesh artifact '{}' (handle {})", record->path.generic_string(),
            handle.value());
        return true;
    }

    AssetManager::MeshImportState AssetManager::get_mesh_import_state(
        const AssetHandle handle) const {
        const auto status = m_async_state->mesh_status.find(handle);
        if(status == m_async_state->mesh_status.end()
            || !m_database.is_current(handle, status->second.revision))
            return MeshImportState::Unknown;
        return status->second.state;
    }

    bool AssetManager::inspect_mesh(const AssetHandle handle) {
        const auto* record = find_asset_record(m_database, handle, AssetType::Mesh);
        return record && schedule_mesh_task(*record, true);
    }

    bool AssetManager::import_mesh_async(const AssetHandle handle) {
        const auto* record = find_asset_record(m_database, handle, AssetType::Mesh);
        return record && schedule_mesh_task(*record, false);
    }

    std::shared_ptr<Mesh> AssetManager::load_mesh(const AssetHandle handle) {
        if(!validate_asset_handle(handle, "load a mesh")) {
            return nullptr;
        }

        const auto runtime = find_runtime_asset<Mesh>(m_registry, handle);
        if(runtime.asset) {
            return runtime.asset;
        }
        if(runtime.type_conflict) {
            return nullptr;
        }

        const AssetRecord* record =
            find_asset_record(m_database, handle, AssetType::Mesh);
        if(!record) {
            return nullptr;
        }
        const AssetRevision revision = m_database.get_revision(handle);

        auto mesh = create_runtime_mesh(*record);
        if(!mesh) {
            return nullptr;
        }
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG(
                "Discarded stale runtime mesh candidate for asset handle {} (revision {})",
                handle.value(), revision);
            return nullptr;
        }
        if(!publish_runtime_asset(m_registry, handle, mesh, false, "mesh")) {
            return nullptr;
        }
        return mesh;
    }

    std::shared_ptr<Texture> AssetManager::load_texture(const AssetHandle handle) {
        if(!validate_asset_handle(handle, "load a texture")) {
            return nullptr;
        }

        const auto runtime = find_runtime_asset<Texture>(m_registry, handle);
        if(runtime.asset) {
            return runtime.asset;
        }
        if(runtime.type_conflict) {
            return nullptr;
        }

        const AssetRecord* record =
            find_asset_record(m_database, handle, AssetType::Texture);
        if(!record) {
            return nullptr;
        }

        const auto* settings =
            std::get_if<TextureImportSettings>(&record->import_settings);
        if(!settings) {
            LOG_ERROR("Texture asset handle {} has incompatible import settings",
                handle.value());
            return nullptr;
        }

        auto texture = create_runtime_texture(*record, *settings);
        if(!texture) {
            return nullptr;
        }
        if(!publish_runtime_asset(m_registry, handle, texture, false, "texture")) {
            return nullptr;
        }
        return texture;
    }

    std::shared_ptr<Texture> AssetManager::reimport_texture(
        const AssetHandle handle, TextureImportSettings import_settings) {
        if(!validate_asset_handle(handle, "reimport a texture")) {
            return nullptr;
        }

        const AssetRecord* record =
            find_asset_record(m_database, handle, AssetType::Texture);
        if(!record) {
            return nullptr;
        }

        const auto runtime = find_runtime_asset<Texture>(m_registry, handle);
        if(runtime.type_conflict) {
            return nullptr;
        }
        const auto& previous_texture = runtime.asset;

        auto texture = create_runtime_texture(*record, import_settings);
        if(!texture) {
            return nullptr;
        }

        try {
            m_database.update_import_settings(handle, import_settings);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        if(!publish_runtime_asset(m_registry, handle, texture,
               static_cast<bool>(previous_texture), "texture")) {
            return nullptr;
        }

        reload_loaded_material_dependents(handle);
        LOG_INFO("Reimported texture asset '{}' (handle {}, color_space={}, flip_y={})",
            record->path.generic_string(), handle.value(),
            to_string(import_settings.color_space), import_settings.flip_y);
        return texture;
    }

    void AssetManager::reload_loaded_material_dependents(
        const AssetHandle texture_handle) {
        const auto dependents = m_database.get_dependents(texture_handle);
        // 材质重载会修改依赖索引，先复制句柄，避免遍历失效。
        const std::vector<AssetHandle> snapshot(dependents.begin(), dependents.end());
        for(const AssetHandle handle : snapshot) {
            const AssetRecord* record = m_database.find(handle);
            if(record && record->type == AssetType::Material
                && m_registry.resolve<Material>(handle) && !reload_material(handle)) {
                LOG_ERROR(
                    "Texture handle {} was updated, but dependent material handle {} could not be refreshed",
                    texture_handle.value(), handle.value());
            }
        }
    }

    std::shared_ptr<Material> AssetManager::load_material(const AssetHandle handle) {
        if(!validate_asset_handle(handle, "load a material")) {
            return nullptr;
        }

        const auto runtime = find_runtime_asset<Material>(m_registry, handle);
        if(runtime.asset) {
            return runtime.asset;
        }
        if(runtime.type_conflict) {
            return nullptr;
        }

        const AssetRecord* record =
            find_asset_record(m_database, handle, AssetType::Material);
        if(!record) {
            return nullptr;
        }

        auto material = create_runtime_material(*record);
        if(!material) {
            return nullptr;
        }
        if(!publish_runtime_asset(m_registry, handle, material, false, "material")) {
            return nullptr;
        }
        return material;
    }

    std::shared_ptr<Material> AssetManager::reload_material(const AssetHandle handle) {
        if(!validate_asset_handle(handle, "reload a material")) {
            return nullptr;
        }

        const AssetRecord* record =
            find_asset_record(m_database, handle, AssetType::Material);
        if(!record) {
            return nullptr;
        }

        const auto runtime = find_runtime_asset<Material>(m_registry, handle);
        if(runtime.type_conflict) {
            return nullptr;
        }
        const bool has_runtime_asset = static_cast<bool>(runtime.asset);

        MaterialData data;
        try {
            data = MaterialSerializer{}.load(m_paths.assets() / record->path);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        auto material = create_runtime_material(*record, data);
        if(!material) {
            return nullptr;
        }

        try {
            m_database.update_dependencies(handle, get_asset_dependencies(data));
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        if(!publish_runtime_asset(
               m_registry, handle, material, has_runtime_asset, "material")) {
            return nullptr;
        }
        LOG_INFO("Reloaded material asset '{}' (handle {})",
            record->path.generic_string(), handle.value());
        return material;
    }

    std::shared_ptr<Material> AssetManager::update_material(
        const AssetHandle handle, const MaterialData& data) {
        if(!validate_asset_handle(handle, "update a material")) {
            return nullptr;
        }

        const AssetRecord* record =
            find_asset_record(m_database, handle, AssetType::Material);
        if(!record) {
            return nullptr;
        }

        const auto runtime = find_runtime_asset<Material>(m_registry, handle);
        if(runtime.type_conflict) {
            return nullptr;
        }
        const bool has_runtime_asset = static_cast<bool>(runtime.asset);

        const MaterialSerializer serializer;
        std::string serialized_data;
        try {
            serialized_data = serializer.serialize(data);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        auto material = create_runtime_material(*record, data);
        if(!material) {
            return nullptr;
        }

        try {
            write_text_file_atomic(m_paths.assets() / record->path, serialized_data);
            m_database.update_dependencies(handle, get_asset_dependencies(data));
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        if(!publish_runtime_asset(
               m_registry, handle, material, has_runtime_asset, "material")) {
            return nullptr;
        }
        LOG_INFO("Updated material asset '{}' (handle {})", record->path.generic_string(),
            handle.value());
        return material;
    }

    std::shared_ptr<Mesh> AssetManager::create_runtime_mesh(const AssetRecord& record) {
        const AssetHandle handle = record.handle;
        const AssetRevision revision = m_database.get_revision(handle);
        const auto artifact =
            MeshArtifact::load(m_import_service->mesh_artifact_path(handle), handle);
        if(!artifact) {
            LOG_ERROR(
                "Mesh artifact is missing or invalid for '{}' (handle {}); import the asset before loading it",
                record.path.generic_string(), handle.value());
            return nullptr;
        }
        if(!m_database.is_current(handle, revision)) {
            return nullptr;
        }
        record_import_dependencies(handle, artifact->source_dependencies());
        auto mesh_attempt = m_resource_factory.try_create_mesh(artifact->data);
        if(!mesh_attempt) {
            LOG_ERROR("Failed to create runtime mesh for asset handle {}: {}",
                handle.value(), vk::to_string(mesh_attempt.result()));
            return nullptr;
        }
        return std::move(mesh_attempt).value();
    }

    void AssetManager::record_import_dependencies(const AssetHandle handle,
        const std::vector<std::filesystem::path>& dependencies) {
        try {
            m_database.update_import_dependencies(handle, dependencies);
        } catch(const std::exception& exception) {
            LOG_WARN("Could not index import dependencies for asset handle {}: {}",
                handle.value(), exception.what());
        }
    }

    bool AssetManager::schedule_loaded_mesh_refresh(const AssetRecord& record) {
        const AssetHandle handle = record.handle;
        const auto previous_mesh = m_registry.resolve<Mesh>(handle);
        if(!previous_mesh) {
            return !m_registry.contains(handle);
        }

        return schedule_mesh_task(record, false);
    }

    bool AssetManager::schedule_mesh_task(
        const AssetRecord& record, const bool inspect_only) {
        const auto handle = record.handle;
        const auto revision = m_database.get_revision(handle);
        if(const auto pending = m_async_state->pending_assets.find(handle);
            pending != m_async_state->pending_assets.end()
            && pending->second == revision) {
            // 不在同一 revision 上并行检查与导入；Importing 请求可合并，Checking 完成后再导入。
            return inspect_only
                   || get_mesh_import_state(handle) == MeshImportState::Importing;
        }
        m_async_state->mesh_status[handle] = {revision,
            inspect_only ? MeshImportState::Checking : MeshImportState::Importing};
        const bool scheduled = schedule_refresh_task(handle, revision, AssetType::Mesh,
            [paths = m_paths, handle, revision, relative_path = record.path,
                inspect_only](ImportResult& result) {
                result.candidate = build_mesh_artifact_candidate(
                    paths, handle, revision, relative_path, inspect_only);
            });
        if(!scheduled)
            m_async_state->mesh_status[handle] = {revision, MeshImportState::Failed};
        return scheduled;
    }

    bool AssetManager::schedule_loaded_texture_refresh(const AssetRecord& record) {
        const AssetHandle handle = record.handle;
        const AssetRevision revision = m_database.get_revision(handle);
        const auto previous_texture = m_registry.resolve<Texture>(handle);
        if(!previous_texture) {
            return !m_registry.contains(handle);
        }

        const auto* settings =
            std::get_if<TextureImportSettings>(&record.import_settings);
        if(!settings) {
            LOG_ERROR("Texture asset handle {} has incompatible import settings",
                handle.value());
            return false;
        }

        return schedule_refresh_task(handle, revision, AssetType::Texture,
            [asset_root = m_paths.assets(), handle, revision, relative_path = record.path,
                settings = *settings](ImportResult& result) {
                result.candidate = import_texture_candidate(
                    asset_root, handle, revision, relative_path, settings);
            });
    }

    bool AssetManager::schedule_refresh_task(const AssetHandle handle,
        const AssetRevision revision, const AssetType type,
        std::function<void(ImportResult&)> task) {
        const auto pending = m_async_state->pending_assets.find(handle);
        if(pending != m_async_state->pending_assets.end()
            && pending->second == revision) {
            return true;
        }

        auto& queue = m_async_state->queued_tasks;
        const auto queued =
            std::ranges::find(queue, handle, &AsyncState::QueuedAssetTask::handle);
        if(queued == queue.end() && queue.size() >= m_async_limits.queued) {
            LOG_WARN(
                "Asset request queue is full; retry asset handle {}", handle.value());
            return false;
        }
        std::optional<AssetRevision> previous_revision;
        if(pending != m_async_state->pending_assets.end())
            previous_revision = pending->second;
        try {
            m_async_state->pending_assets[handle] = revision;
            if(queued != queue.end())
                *queued = {handle, revision, type, std::move(task)};
            else
                queue.push_back({handle, revision, type, std::move(task)});
        } catch(const std::exception& exception) {
            if(previous_revision)
                m_async_state->pending_assets[handle] = *previous_revision;
            else
                m_async_state->pending_assets.erase(handle);
            LOG_ERROR("Failed to schedule {} refresh for asset handle {}: {}",
                to_string(type), handle.value(), exception.what());
            return false;
        }
        dispatch_queued_tasks();
        return true;
    }

    void AssetManager::dispatch_queued_tasks() {
        auto& queue = m_async_state->queued_tasks;
        auto& scheduled = m_async_state->scheduled_tasks;
        for(auto request = queue.begin();
            request != queue.end() && scheduled.size() < m_async_limits.in_flight;) {
            const auto clear_pending = [&] {
                const auto pending = m_async_state->pending_assets.find(request->handle);
                if(pending != m_async_state->pending_assets.end()
                    && pending->second == request->revision)
                    m_async_state->pending_assets.erase(pending);
            };
            if(!m_database.is_current(request->handle, request->revision)) {
                clear_pending();
                request = queue.erase(request);
                continue;
            }
            if(std::ranges::any_of(scheduled,
                   [&](const auto& task) { return task.handle == request->handle; })) {
                ++request;
                continue;
            }
            scheduled.push_back(
                {request->handle, request->revision, request->type, {}, {}});
            try {
                auto result = std::make_shared<ImportResult>();
                auto completion = m_task_scheduler.try_submit(
                    [result, task = request->task] { task(*result); });
                if(!completion) {
                    scheduled.pop_back();
                    break;
                }
                scheduled.back().completion = std::move(*completion);
                scheduled.back().result = std::move(result);
            } catch(const std::exception& error) {
                scheduled.pop_back();
                clear_pending();
                if(request->type == AssetType::Mesh)
                    m_async_state->mesh_status[request->handle] = {
                        request->revision, MeshImportState::Failed};
                LOG_ERROR("Failed to dispatch asset handle {}: {}",
                    request->handle.value(), error.what());
            }
            request = queue.erase(request);
        }
    }

    std::shared_ptr<Texture> AssetManager::create_runtime_texture(
        const AssetRecord& record, const TextureImportSettings& import_settings) {
        TextureData data;
        try {
            data =
                TextureImporter{}.import(m_paths.assets() / record.path, import_settings);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }
        auto texture_attempt = m_resource_factory.try_create_texture(data);
        if(!texture_attempt) {
            LOG_ERROR("Failed to create runtime texture for asset handle {}: {}",
                record.handle.value(), vk::to_string(texture_attempt.result()));
            return nullptr;
        }
        return std::move(texture_attempt).value();
    }

    std::shared_ptr<Material> AssetManager::create_runtime_material(
        const AssetRecord& record) {
        MaterialData data;
        try {
            data = MaterialSerializer{}.load(m_paths.assets() / record.path);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        return create_runtime_material(record, data);
    }

    std::shared_ptr<Material> AssetManager::create_runtime_material(
        const AssetRecord& record, const MaterialData& data) {
        std::map<std::string, std::shared_ptr<Texture>> textures;
        for(const auto& [property_name, texture_handle] : data.texture_properties) {
            auto texture = load_texture(texture_handle);
            if(!texture) {
                LOG_ERROR(
                    "Failed to resolve texture handle {} for material '{}' property '{}'",
                    texture_handle.value(), record.path.generic_string(), property_name);
                return nullptr;
            }
            textures.emplace(property_name, std::move(texture));
        }

        auto material =
            std::make_shared<Material>(record.path.stem().string(), data.template_name);
        for(const auto& [property_name, texture] : textures) {
            material->set_texture_property(property_name, texture);
        }
        return material;
    }
}
