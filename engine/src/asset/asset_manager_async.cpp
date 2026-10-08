#include "asset/asset_manager.h"
#include "asset/runtime/render_asset_publisher.h"
#include "graphics/error.h"

#include "asset/import/asset_task_queue.h"
#include "asset/import/import_candidate.h"
#include "asset/import/import_service.h"
#include "asset/import/environment_importer.h"
#include "asset/import/mesh_importer.h"
#include "asset/import/texture_importer.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "diagnostics/logger.h"
#include "common/scope_exit.h"

#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace Comet {
    struct AssetManager::PendingEnvironmentPreview {
        AssetRevision revision;
        EnvironmentImporter::Preview preview;
    };

    AssetAsyncStatus AssetManager::get_async_status() const {
        return m_task_queue->status();
    }

    void AssetManager::accept_scan_report(const AssetScanReport& report) {
        if(!report.snapshot_updated) {
            return;
        }

        std::unordered_set<AssetHandle> invalidated(
            report.removed_assets.begin(), report.removed_assets.end());
        const std::unordered_set<AssetHandle> removed(
            report.removed_assets.begin(), report.removed_assets.end());
        m_database.include_dependents(invalidated);
        for(const AssetHandle handle : invalidated) {
            const auto* record = m_database.find(handle);
            // 源资产失效不清掉程序的最后一个成功版本；程序本身删除时才清除。
            if(record && record->type == AssetType::ShaderProgram && !removed.contains(handle))
                continue;
            static_cast<void>(m_registry.unregister_asset(handle));
            m_failed_environments.erase(handle);
            m_environment_previews.erase(handle);
        }
        for(const AssetHandle handle : report.removed_assets)
            m_mesh_imports_needing_recheck.erase(handle);

        std::vector<AssetHandle> scripts;
        for(const AssetHandle handle : report.modified_assets) {
            const bool preparing_environment = m_environment_previews.contains(handle);
            if(invalidated.contains(handle)
                || (!m_registry.contains(handle) && !preparing_environment)) {
                continue;
            }

            const AssetRecord* record = m_database.find(handle);
            if(!record) {
                continue;
            }
            if(preparing_environment) {
                const auto current = m_render_assets->environment(handle);
                if(current && !m_render_assets->has_lighting(current))
                    static_cast<void>(m_registry.unregister_asset(handle));
                m_environment_previews.erase(handle);
            }
            // 编辑器提供编译任务，AssetManager 仅在候选完成后发布。
            if(record->type == AssetType::ShaderProgram)
                continue;
            if(record->type == AssetType::Script) {
                scripts.push_back(handle);
                continue;
            }
            if(schedule_refresh(*record) == RefreshResult::Deferred)
                m_refresh_requests[handle] = m_database.get_revision(handle);
            else
                m_refresh_requests.erase(handle);
        }
        refresh_scripts(scripts);
    }

    AssetManager::RefreshResult AssetManager::schedule_refresh(const AssetRecord& record) {
        bool accepted = false;
        switch(record.type) {
            case AssetType::Audio:
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
                if(!m_render_assets->texture(record.handle)
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

    void AssetManager::retry_refresh_requests(std::vector<AssetHandle>& published) {
        std::vector<AssetHandle> scripts;
        for(auto request = m_refresh_requests.begin(); request != m_refresh_requests.end();) {
            const auto [handle, revision] = *request;
            const auto* record = m_database.find(handle);
            if(!record || !m_database.is_current(handle, revision)
                || (!m_registry.contains(handle) && record->type != AssetType::Environment
                    && record->type != AssetType::Script)) {
                request = m_refresh_requests.erase(request);
                continue;
            }
            if(record->type == AssetType::Script) {
                scripts.push_back(handle);
                request = m_refresh_requests.erase(request);
                continue;
            }
            if(schedule_refresh(*record) == RefreshResult::Deferred)
                break;
            request = m_refresh_requests.erase(request);
        }
        const auto completed_scripts = refresh_scripts(scripts);
        published.insert(published.end(), completed_scripts.begin(), completed_scripts.end());
    }

    Result<std::vector<AssetHandle>, Error> AssetManager::process_completions() {
        return process_completions(AssetCompletionBudget{});
    }

    Result<std::vector<AssetHandle>, Error> AssetManager::process_completions(
        const AssetCompletionBudget budget) {
        if(m_processing_completions) {
            LOG_WARN("Ignoring reentrant asset completion processing");
            return Result<std::vector<AssetHandle>, Error>::success({});
        }
        m_processing_completions = true;
        const ScopeExit reset_processing([this] { m_processing_completions = false; });
        const auto start = std::chrono::steady_clock::now();
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
        retry_refresh_requests(published);
        auto remaining = budget;
        remaining.max_results -= completion.value();
        remaining.max_time -= std::chrono::steady_clock::now() - start;
        if(auto previews = publish_environment_previews(remaining); !previews)
            return Result<std::vector<AssetHandle>, Error>::failure(previews.error());
        return Result<std::vector<AssetHandle>, Error>::success(std::move(published));
    }

    Result<void, Error> AssetManager::publish_environment_previews(
        const AssetCompletionBudget budget) {
        const auto start = std::chrono::steady_clock::now();
        for(auto item = m_environment_previews.begin(); item != m_environment_previews.end();) {
            const auto [handle, pending] = *item;
            if(!m_database.is_current(handle, pending->revision)
                || !m_task_queue->contains(handle, pending->revision)) {
                item = m_environment_previews.erase(item);
                continue;
            }
            ++item;
            if(budget.max_results == 0
                || std::chrono::steady_clock::now() - start >= budget.max_time)
                break;
            if(m_registry.contains(handle))
                continue;
            auto background = pending->preview.take();
            if(!background)
                continue;
            auto environment = m_render_assets->prepare_preview(*background);
            if(!environment) {
                if(is_device_lost(environment.error()))
                    return Result<void, Error>::failure(environment.error());
                LOG_WARN("Cannot upload environment preview: {}", environment.error().message);
                return Result<void, Error>::success();
            }
            if(!m_database.is_current(handle, pending->revision))
                return Result<void, Error>::success();
            static_cast<void>(m_render_assets->publish(handle, environment.value(), false));
            return Result<void, Error>::success();
        }
        return Result<void, Error>::success();
    }

    bool AssetManager::record_import_dependencies(
        const AssetHandle handle, const std::vector<std::filesystem::path>& dependencies) {
        if(auto updated = m_database.update_import_dependencies(handle, dependencies); !updated) {
            LOG_WARN("Could not index import dependencies for asset handle {}: {}", handle.value(),
                updated.error());
            return false;
        }
        return true;
    }

    void AssetManager::complete_mesh_import(const AssetHandle handle, const AssetRevision revision,
        const std::vector<std::filesystem::path>& dependencies) {
        if(record_import_dependencies(handle, dependencies)
            && !m_task_queue->contains(handle, revision))
            m_mesh_imports_needing_recheck.erase(handle);
    }

    bool AssetManager::schedule_mesh_task(const AssetRecord& record, const MeshImportMode mode) {
        const auto handle = record.handle;
        const auto revision = m_database.get_revision(handle);
        const auto source = m_database.paths().assets() / record.path;
        std::error_code error;
        const auto source_size = std::filesystem::file_size(source, error);
        Result<std::size_t> estimate = Result<std::size_t>::success(m_limits.mesh_working_bytes);
        if(error || source_size <= m_limits.mesh_owner_inspect_bytes)
            estimate = MeshImporter::working_bytes(source, m_limits);
        std::string failure;
        if(!estimate)
            failure = estimate.error();
        else if(estimate.value() > m_task_queue->memory_budget())
            failure = "Mesh exceeds the asset CPU memory budget";
        if(!failure.empty()) {
            const bool accepted = m_task_queue->schedule(
                handle, revision,
                [handle, revision, path = record.path, message = std::move(failure)](
                    AssetImportResult& result) {
                    result.candidate = MeshArtifactCandidate{
                        handle, revision, path, Result<MeshArtifact>::failure(message)};
                },
                mode == MeshImportMode::Force);
            if(accepted)
                m_mesh_imports_needing_recheck.insert(handle);
            return accepted;
        }
        const auto budget = estimate.value();
        const bool scheduled = m_task_queue->schedule(
            handle, revision,
            [paths = m_database.paths(), limits = m_limits, record, revision, mode, budget](
                AssetImportResult& result) {
                result.candidate =
                    ImportService(paths, limits).prepare_mesh(record, revision, mode, budget);
            },
            mode == MeshImportMode::Force, budget);
        if(scheduled)
            m_mesh_imports_needing_recheck.insert(handle);
        return scheduled;
    }

    bool AssetManager::import_shader_program_async(
        ShaderProgramImportRequest request, ShaderProgramPrepare prepare) {
        const auto* record = m_database.find(request.handle);
        if(!record || record->type != AssetType::ShaderProgram
            || !m_database.is_current(request.handle, request.revision))
            return false;
        return m_task_queue->schedule(request.handle, request.revision,
            [request = std::move(request), prepare = std::move(prepare)](
                AssetImportResult& result) {
                result.candidate = ShaderProgramImportCandidate{request, prepare()};
            });
    }

    bool AssetManager::schedule_material_refresh(const AssetRecord& record) {
        const auto revision = m_database.get_revision(record.handle);
        return m_task_queue->schedule(record.handle, revision,
            [record, revision, root = m_database.paths().assets()](AssetImportResult& result) {
                result.candidate = MaterialImportCandidate{
                    record, revision, MaterialSerializer{}.load(root / record.path)};
            });
    }

    bool AssetManager::schedule_loaded_texture_refresh(const AssetRecord& record) {
        const AssetHandle handle = record.handle;
        const AssetRevision revision = m_database.get_revision(handle);
        if(m_task_queue->contains(handle, revision))
            return true;
        const auto previous_texture = m_render_assets->texture(handle);
        if(!previous_texture) {
            return !m_registry.contains(handle);
        }

        const auto* settings = std::get_if<TextureImportSettings>(&record.import_settings);
        if(!settings) {
            LOG_ERROR("Texture asset handle {} has incompatible import settings", handle.value());
            return false;
        }

        auto bytes =
            TextureImporter::working_bytes(m_database.paths().assets() / record.path, m_limits);
        if(!bytes || bytes.value() > m_task_queue->memory_budget()) {
            const std::string message =
                bytes ? "Texture exceeds the asset CPU memory budget" : bytes.error();
            return m_task_queue->schedule(handle, revision,
                [handle, revision, path = record.path, message](AssetImportResult& result) {
                    result.candidate = TextureImportCandidate{
                        handle, revision, path, Result<TextureData>::failure(message)};
                });
        }

        return m_task_queue->schedule(
            handle, revision,
            [paths = m_database.paths(), limits = m_limits, handle, revision, record,
                settings = *settings, budget = bytes.value()](AssetImportResult& result) {
                result.candidate = TextureImportCandidate{handle, revision, record.path,
                    ImportService(paths, limits).prepare_texture(record, settings, budget)};
            },
            false, bytes.value());
    }

    Result<bool, Error> AssetManager::schedule_environment(const AssetRecord& record) {
        const auto revision = m_database.get_revision(record.handle);
        if(m_task_queue->contains(record.handle, revision))
            return Result<bool, Error>::success(true);
        auto bytes = EnvironmentImporter::working_bytes(m_database.paths().assets() / record.path);
        if(!bytes)
            return Result<bool, Error>::failure({bytes.error()});
        if(bytes.value() > m_task_queue->memory_budget())
            return Result<bool, Error>::failure(
                {"Environment exceeds the asset CPU memory budget"});
        auto preview = std::make_shared<PendingEnvironmentPreview>();
        preview->revision = revision;
        const bool needs_preview = !m_registry.contains(record.handle);
        const auto accepted = m_task_queue->schedule(
            record.handle, revision,
            [paths = m_database.paths(), record, revision, budget = bytes.value(), preview,
                needs_preview](AssetImportResult& result) {
                EnvironmentImporter::Preview* output = nullptr;
                if(needs_preview)
                    output = &preview->preview;
                auto prepared = ImportService(paths).prepare_environment(record, budget, output);
                auto data = Result<EnvironmentData>::failure("Environment preparation failed");
                if(prepared)
                    data = Result<EnvironmentData>::success(std::move(prepared).value().data);
                else
                    data = Result<EnvironmentData>::failure(prepared.error());
                result.candidate = EnvironmentImportCandidate{
                    record.handle, revision, record.path, std::move(data)};
            },
            false, bytes.value());
        if(accepted)
            m_environment_previews[record.handle] = std::move(preview);
        return Result<bool, Error>::success(accepted);
    }

}
