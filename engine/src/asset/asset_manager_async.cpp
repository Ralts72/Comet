#include "asset/asset_manager.h"

#include "asset/import/asset_task_queue.h"
#include "asset/import/import_candidate.h"
#include "asset/import/import_service.h"
#include "asset/import/environment_importer.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "diagnostics/logger.h"
#include "render/resource/texture.h"

#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace Comet {
    AssetAsyncStatus AssetManager::get_async_status() const {
        return m_task_queue->status();
    }

    void AssetManager::apply_scan_report(const AssetScanReport& report) {
        if(!report.snapshot_updated) {
            return;
        }

        std::unordered_set<AssetHandle> invalidated(
            report.removed_assets.begin(), report.removed_assets.end());
        m_database.include_dependents(invalidated);
        for(const AssetHandle handle : invalidated) {
            static_cast<void>(m_registry.unregister_asset(handle));
            m_failed_environments.erase(handle);
        }

        for(const AssetHandle handle : report.modified_assets) {
            if(invalidated.contains(handle) || !m_registry.contains(handle)) {
                continue;
            }

            const AssetRecord* record = m_database.find(handle);
            if(!record) {
                continue;
            }
            if(schedule_refresh(*record) == RefreshResult::Deferred)
                m_refresh_requests[handle] = m_database.get_revision(handle);
            else
                m_refresh_requests.erase(handle);
        }
    }

    AssetManager::RefreshResult AssetManager::schedule_refresh(const AssetRecord& record) {
        bool accepted = false;
        switch(record.type) {
            case AssetType::Audio:
            case AssetType::Script:
                // 活动实例保留旧资源；下次准备场景时加载新版，不在运行中替换实例。
                static_cast<void>(m_registry.unregister_asset(record.handle));
                return RefreshResult::Invalidated;
            case AssetType::Mesh:
                accepted = schedule_mesh_task(record, MeshImportMode::Force);
                break;
            case AssetType::Material:
                accepted = schedule_material_refresh(record);
                break;
            case AssetType::Texture:
                if(!m_registry.resolve<Texture>(record.handle)
                    || !std::holds_alternative<TextureImportSettings>(record.import_settings)) {
                    LOG_ERROR(
                        "Cannot refresh texture asset handle {}: incompatible runtime type or settings",
                        record.handle.value());
                    return RefreshResult::Rejected;
                }
                accepted = schedule_loaded_texture_refresh(record);
                break;
            case AssetType::Environment: {
                auto scheduled = schedule_environment(record);
                if(!scheduled) {
                    LOG_WARN("Cannot refresh environment: {}", scheduled.error().message);
                    return RefreshResult::Rejected;
                }
                accepted = scheduled.value();
                break;
            }
            default:
                static_cast<void>(m_registry.unregister_asset(record.handle));
                LOG_WARN(
                    "Unloaded modified asset handle {} because runtime reload is not implemented for type '{}'",
                    record.handle.value(), to_string(record.type));
                return RefreshResult::Invalidated;
        }
        return accepted ? RefreshResult::Scheduled : RefreshResult::Deferred;
    }

    void AssetManager::retry_refresh_requests() {
        for(auto request = m_refresh_requests.begin(); request != m_refresh_requests.end();) {
            const auto [handle, revision] = *request;
            const auto* record = m_database.find(handle);
            if(!record || !m_database.is_current(handle, revision)
                || (!m_registry.contains(handle) && record->type != AssetType::Environment)) {
                request = m_refresh_requests.erase(request);
                continue;
            }
            if(schedule_refresh(*record) == RefreshResult::Deferred)
                break;
            request = m_refresh_requests.erase(request);
        }
    }

    Result<std::vector<AssetHandle>, Error> AssetManager::process_completions() {
        return process_completions(AssetCompletionBudget{});
    }

    Result<std::vector<AssetHandle>, Error> AssetManager::process_completions(
        const AssetCompletionBudget budget) {
        std::vector<AssetHandle> published;
        auto completion = m_task_queue->process_completions(budget, [&](AssetImportResult& result) {
            auto publication = publish_import_result(result);
            if(!publication)
                return Result<void, Error>::failure(publication.error());
            if(publication.value())
                published.push_back(*publication.value());
            return Result<void, Error>::success();
        });
        if(!completion)
            return Result<std::vector<AssetHandle>, Error>::failure(completion.error());
        retry_refresh_requests();
        return Result<std::vector<AssetHandle>, Error>::success(std::move(published));
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
            [paths = m_paths, record, revision, mode](AssetImportResult& result) {
                result.candidate = ImportService(paths).prepare_mesh(record, revision, mode);
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
            [paths = m_paths, handle, revision, record, settings = *settings](
                AssetImportResult& result) {
                result.candidate = TextureImportCandidate{handle, revision, record.path,
                    ImportService(paths).prepare_texture(record, settings)};
            });
    }

    Result<bool, Error> AssetManager::schedule_environment(const AssetRecord& record) {
        const auto revision = m_database.get_revision(record.handle);
        if(m_task_queue->contains(record.handle, revision))
            return Result<bool, Error>::success(true);
        auto bytes = EnvironmentImporter::working_bytes(m_paths.assets() / record.path);
        if(!bytes)
            return Result<bool, Error>::failure({bytes.error()});
        if(bytes.value() > m_task_queue->memory_budget())
            return Result<bool, Error>::failure(
                {"Environment exceeds the asset CPU memory budget"});
        const auto accepted = m_task_queue->schedule(
            record.handle, revision,
            [paths = m_paths, record, revision, budget = bytes.value()](AssetImportResult& result) {
                auto prepared = ImportService(paths).prepare_environment(record, budget);
                auto data = Result<EnvironmentData>::failure("Environment preparation failed");
                if(prepared)
                    data = Result<EnvironmentData>::success(std::move(prepared).value().data);
                else
                    data = Result<EnvironmentData>::failure(prepared.error());
                result.candidate = EnvironmentImportCandidate{
                    record.handle, revision, record.path, std::move(data)};
            },
            false, bytes.value());
        return Result<bool, Error>::success(accepted);
    }

}
