#include "assets/editor_assets.h"
#include "assets/shader_program_import.h"
#include "assets/system_trash.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "asset/serialization/material_serializer.h"
#include "assets/source_operations.h"
#include "core/task_scheduler.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace CometEditor {
    EditorAssets::EditorAssets(Comet::ProjectPaths paths, Comet::AssetRegistry& registry,
        Comet::RenderResourceFactory& factory, Comet::TaskScheduler& scheduler,
        const std::chrono::milliseconds quiet_period, const Comet::AssetImportLimits limits,
        AssetSourceOperations::TrashMover trash_mover)
        : m_paths(std::move(paths)), m_limits(limits), m_trash_mover(std::move(trash_mover)),
          m_database(m_paths), m_manager(m_database, registry, factory, scheduler, m_limits),
          m_scene_assets(m_database, m_manager), m_monitor(m_paths.assets()),
          m_scheduler(scheduler), m_quiet_period(quiet_period) {
        if(!m_trash_mover)
            m_trash_mover = SystemTrash::move;
        std::error_code error;
        const auto pending = m_paths.local_data() / "pending-deletions";
        if(std::filesystem::is_directory(pending, error)
            && !std::filesystem::is_empty(pending, error))
            LOG_WARN("Unfinished asset deletion remains at '{}'; inspect it before cleanup",
                pending.string());
        if(!m_monitor.uses_native_notifications())
            LOG_WARN("Asset source monitor is using periodic fallback scans");
    }

    Comet::Result<Comet::MaterialData> EditorAssets::read_material(const AssetRead& request) const {
        const auto* record = database().find(request.handle);
        if(!record || record->type != Comet::AssetType::Material
            || !database().is_current(request.handle, request.revision))
            return Comet::Result<Comet::MaterialData>::failure("Material read request is stale");
        return Comet::MaterialSerializer{}.load(m_paths.assets() / record->path);
    }

    void EditorAssets::observe(const AssetSourceMonitor::PollResult& result) {
        using State = AssetSourceMonitor::PollState;
        if(result.state == State::NotPolled)
            return;
        if(result.state != State::Failed) {
            m_monitor_error.clear();
            return;
        }
        const auto error = result.issue_path.generic_string() + ": " + result.message;
        if(error != m_monitor_error) {
            LOG_WARN("Asset source monitor: {}", error);
            m_monitor_error = error;
        }
    }

    void EditorAssets::acknowledge(const std::filesystem::path& path) {
        if(!path.empty())
            static_cast<void>(m_monitor.acknowledge(path));
    }

    void EditorAssets::accept_scan(
        const Comet::AssetScanReport& report, const std::optional<Clock::time_point> change_time) {
        PROFILE_SCOPE("EditorAssets::accept_scan");
        m_manager.accept_scan_report(report);
        if(report.snapshot_updated) {
            std::unordered_set<Comet::AssetHandle> changed;
            changed.insert(report.added_assets.begin(), report.added_assets.end());
            changed.insert(report.modified_assets.begin(), report.modified_assets.end());
            database().include_dependents(changed);
            for(const auto handle : changed) {
                const auto* record = database().find(handle);
                if(!record)
                    continue;
                if(record->type == Comet::AssetType::Mesh)
                    m_pending_mesh_imports.try_emplace(handle, Comet::MeshImportMode::IfNeeded);
                if(record->type == Comet::AssetType::ShaderProgram) {
                    auto& due = m_pending_shader_programs[handle];
                    if(change_time)
                        due = *change_time + m_quiet_period;
                    else
                        due = Clock::time_point{};
                }
            }
            for(const auto handle : report.removed_assets)
                m_pending_shader_programs.erase(handle);
            std::erase_if(m_pending_mesh_imports, [&](const auto& entry) {
                const auto* record = database().find(entry.first);
                return !record || record->type != Comet::AssetType::Mesh;
            });
            // 首次导入失败时依赖可能尚未入库，后续文件变化仍需重试它。
            for(const auto handle : m_manager.mesh_imports_needing_recheck())
                m_pending_mesh_imports.try_emplace(handle, Comet::MeshImportMode::IfNeeded);
        }
        m_scene_assets.accept_scan(report);
        if(report.generated_metadata) {
            for(const auto handle : report.added_assets) {
                if(const auto* record = database().find(handle))
                    acknowledge(Comet::metadata_path(record->path));
            }
        }
        for(const auto& issue : report.issues)
            LOG_WARN("Asset scan issue at '{}': {}", issue.path.generic_string(), issue.message);
    }

    Comet::AssetScanReport EditorAssets::refresh() {
        PROFILE_SCOPE("EditorAssets::refresh");
        m_pending_scan.reset();
        m_full_scan_requested = false;
        observe(m_monitor.poll_now());
        auto report = m_database.scan();
        accept_scan(report);
        if(report.snapshot_updated) {
            for(const auto& record : database().get_assets()) {
                if(record.type == Comet::AssetType::Mesh)
                    m_pending_mesh_imports.try_emplace(
                        record.handle, Comet::MeshImportMode::IfNeeded);
            }
        }
        return report;
    }

    Comet::Result<std::optional<Comet::AssetScanReport>, Comet::Error> EditorAssets::update(
        const Clock::time_point now) {
        PROFILE_SCOPE("EditorAssets::update");
        auto report = update_source_scan(now);
        advance_file_import(report);
        submit_pending_scan();
        schedule_shader_program_imports(now);
        schedule_mesh_imports();
        auto completed = m_manager.process_completions();
        if(!completed)
            return Comet::Result<std::optional<Comet::AssetScanReport>, Comet::Error>::failure(
                completed.error());
        m_scene_assets.mark_changed(completed.value());
        return Comet::Result<std::optional<Comet::AssetScanReport>, Comet::Error>::success(
            std::move(report));
    }

    std::optional<Comet::AssetScanReport> EditorAssets::update_source_scan(
        const Clock::time_point now) {
        const auto result = m_monitor.poll_async(m_scheduler, now);
        observe(result);
        if(result.state == AssetSourceMonitor::PollState::Failed)
            m_full_scan_requested = false;
        std::optional<Comet::AssetScanReport> report;
        if(result.state == AssetSourceMonitor::PollState::Changed) {
            if(!result.requires_full_scan && !m_pending_scan && !m_full_scan_requested)
                report = m_database.scan_changed_sources(result.changed_paths);
            if(report)
                accept_scan(*report, now);
            else {
                m_full_scan_requested = true;
                m_full_scan_change_time = now;
            }
        }
        if(m_pending_scan
            && m_pending_scan->completion.wait_for(std::chrono::seconds(0))
                   == std::future_status::ready) {
            auto prepared = m_pending_scan->completion.get();
            if(m_pending_scan->monitor_generation == m_monitor.change_generation()) {
                report = m_database.publish_scan(std::move(prepared));
                if(report)
                    accept_scan(*report, m_pending_scan->change_time);
            }
            if(!report && result.state != AssetSourceMonitor::PollState::Failed) {
                m_full_scan_requested = true;
                if(m_full_scan_change_time == Clock::time_point{})
                    m_full_scan_change_time = now;
            }
            m_pending_scan.reset();
        }
        return report;
    }

    void EditorAssets::advance_file_import(std::optional<Comet::AssetScanReport>& report) {
        if(m_pending_file_import
            && m_pending_file_import->completion.wait_for(std::chrono::seconds(0))
                   == std::future_status::ready) {
            auto prepared = m_pending_file_import->completion.get();
            Comet::AssetScanReport import_report;
            if(prepared) {
                import_report = std::move(prepared).value().publish(m_database);
            } else {
                import_report.indexed_assets = m_database.size();
                import_report.issues.push_back(
                    {m_pending_file_import->directory, prepared.error()});
            }
            if(import_report.snapshot_updated) {
                observe(m_monitor.poll_now());
                LOG_INFO("Imported {} asset(s) into assets/{}", import_report.added_assets.size(),
                    m_pending_file_import->directory.generic_string());
            }
            accept_scan(import_report);
            if(report && !import_report.snapshot_updated) {
                report->issues.insert(
                    report->issues.end(), import_report.issues.begin(), import_report.issues.end());
            } else {
                if(report)
                    import_report.issues.insert(
                        import_report.issues.end(), report->issues.begin(), report->issues.end());
                report = std::move(import_report);
            }
            m_pending_file_import.reset();
        }
        if(!m_pending_file_import && !m_file_import_requests.empty()) {
            auto& request = m_file_import_requests.front();
            auto completion = m_scheduler.try_submit_result(
                [paths = m_paths, sources = request.sources, directory = request.directory,
                    limits = m_limits] {
                    return AssetSourceOperations::PreparedFileImport::prepare(
                        paths, sources, directory, limits);
                });
            if(completion) {
                m_pending_file_import.emplace(std::move(*completion), request.directory);
                m_file_import_requests.pop_front();
            }
        }
    }

    void EditorAssets::submit_pending_scan() {
        if(m_full_scan_requested && !m_pending_scan) {
            const auto database_generation = m_database.generation();
            auto completion = m_scheduler.try_submit_result([paths = m_paths, database_generation] {
                return Comet::AssetDatabase::prepare_scan(paths, database_generation);
            });
            if(completion) {
                m_pending_scan.emplace(
                    std::move(*completion), m_monitor.change_generation(), m_full_scan_change_time);
                m_full_scan_requested = false;
                m_full_scan_change_time = Clock::time_point{};
            }
        }
    }

    void EditorAssets::schedule_mesh_imports() {
        for(auto request = m_pending_mesh_imports.begin();
            request != m_pending_mesh_imports.end();) {
            const auto* record = database().find(request->first);
            if(!record || record->type != Comet::AssetType::Mesh) {
                request = m_pending_mesh_imports.erase(request);
                continue;
            }
            if(m_manager.is_mesh_import_pending(request->first)) {
                ++request;
                continue;
            }
            if(!m_manager.import_mesh_async(request->first, request->second))
                break;
            request = m_pending_mesh_imports.erase(request);
        }
    }

    std::shared_ptr<const Comet::ShaderProgramArtifact> EditorAssets::compiled_shader_program(
        const Comet::AssetHandle handle) const {
        return m_manager.compiled_shader_program(handle);
    }

    void EditorAssets::schedule_shader_program_imports(const Clock::time_point now) {
        for(auto pending = m_pending_shader_programs.begin();
            pending != m_pending_shader_programs.end();) {
            if(now < pending->second) {
                ++pending;
                continue;
            }
            const auto handle = pending->first;
            const auto* record = database().find(handle);
            if(!record || record->type != Comet::AssetType::ShaderProgram) {
                pending = m_pending_shader_programs.erase(pending);
                continue;
            }
            auto request = ShaderProgramImport::resolve(database(), m_paths, handle);
            if(!request) {
                LOG_WARN("Shader program {}: {}", handle.value(), request.error());
                pending = m_pending_shader_programs.erase(pending);
                continue;
            }
            const auto input = std::move(request).value();
            if(!m_manager.import_shader_program_async(input,
                   [paths = m_paths, input] { return ShaderProgramImport::prepare(paths, input); }))
                break;
            pending = m_pending_shader_programs.erase(pending);
        }
    }

    Comet::AssetScanReport EditorAssets::move(
        const Comet::AssetHandle handle, const std::filesystem::path& destination) {
        const auto* previous = database().find(handle);
        const auto old_path = previous ? previous->path : std::filesystem::path{};
        auto report = AssetSourceOperations::move(m_database, m_paths, handle, destination);
        if(report.snapshot_updated) {
            if(const auto* current = database().find(handle)) {
                acknowledge(old_path);
                if(!old_path.empty())
                    acknowledge(Comet::metadata_path(old_path));
                acknowledge(current->path);
                acknowledge(Comet::metadata_path(current->path));
                LOG_INFO("Moved asset from '{}' to '{}'", old_path.generic_string(),
                    current->path.generic_string());
            }
        }
        accept_scan(report);
        return report;
    }

    Comet::AssetScanReport EditorAssets::remove(const Comet::AssetHandle handle) {
        const auto* previous = database().find(handle);
        const auto old_path = previous ? previous->path : std::filesystem::path{};
        auto report =
            AssetSourceOperations::remove_asset(m_database, m_paths, handle, m_trash_mover);
        if(report.snapshot_updated) {
            acknowledge(old_path);
            acknowledge(Comet::metadata_path(old_path));
        }
        accept_scan(report);
        return report;
    }

    Comet::Result<void> EditorAssets::queue_import_files(
        const std::span<const std::filesystem::path> sources,
        const std::filesystem::path& directory) {
        if(m_file_import_requests.size() >= m_limits.external_file_queue)
            return Comet::Result<void>::failure("External file import queue is full");
        m_file_import_requests.push_back({{sources.begin(), sources.end()}, directory});
        LOG_INFO(
            "Queued {} external file(s) for assets/{}", sources.size(), directory.generic_string());
        return Comet::Result<void>::success();
    }

    Comet::AssetScanReport EditorAssets::create_material(
        const std::filesystem::path& destination, const Comet::MaterialData& data) {
        auto report =
            AssetSourceOperations::create_material(m_database, m_paths, destination, data);
        if(report.snapshot_updated) {
            acknowledge(destination);
            acknowledge(Comet::metadata_path(destination));
        }
        accept_scan(report);
        return report;
    }

    Comet::AssetScanReport EditorAssets::create_script(const std::filesystem::path& destination) {
        auto report = AssetSourceOperations::create_script(m_database, m_paths, destination);
        if(report.snapshot_updated) {
            acknowledge(destination);
            acknowledge(Comet::metadata_path(destination));
        }
        accept_scan(report);
        return report;
    }

    Comet::Result<Comet::AssetManager::MaterialUpdate, Comet::Error> EditorAssets::
        prepare_material_edit(const AssetEdit& edit) {
        using Preparation = Comet::Result<Comet::AssetManager::MaterialUpdate, Comet::Error>;
        if(!database().is_current(edit.handle, edit.revision))
            return Preparation::failure({"Asset edit revision is stale"});
        const auto* material = std::get_if<MaterialEdit>(&edit.value);
        if(!material)
            return Preparation::failure({"Asset edit is not a material edit"});
        return m_manager.prepare_material_update(edit.handle, material->after);
    }

    Comet::Result<void, Comet::Error> EditorAssets::commit_material_edit(
        const Comet::AssetManager::MaterialUpdate& update) {
        auto committed = m_manager.commit_material_update(update);
        if(!committed)
            return Comet::Result<void, Comet::Error>::failure(committed.error());
        acknowledge(database().find(update.handle())->path);
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<void, Comet::Error> EditorAssets::apply_texture_edit(const AssetEdit& edit) {
        if(!database().is_current(edit.handle, edit.revision))
            return Comet::Result<void, Comet::Error>::failure({"Asset edit revision is stale"});

        const auto* texture = std::get_if<TextureEdit>(&edit.value);
        if(!texture)
            return Comet::Result<void, Comet::Error>::failure({"Asset edit is not a texture edit"});
        const auto path = database().find(edit.handle)->path;
        if(auto imported = m_manager.reimport_texture(edit.handle, texture->after); !imported)
            return Comet::Result<void, Comet::Error>::failure(imported.error());
        m_scene_assets.mark_changed(edit.handle);
        acknowledge(Comet::metadata_path(path));
        return Comet::Result<void, Comet::Error>::success();
    }

    void EditorAssets::request_mesh_reimport(const Comet::AssetHandle handle) {
        const auto* record = database().find(handle);
        if(!record || record->type != Comet::AssetType::Mesh)
            return;
        if(m_manager.import_mesh_async(handle, Comet::MeshImportMode::Force))
            m_pending_mesh_imports.erase(handle);
        else
            m_pending_mesh_imports[handle] = Comet::MeshImportMode::Force;
    }

    Comet::Result<void, Comet::Error> EditorAssets::load_reference(const Comet::AssetHandle handle,
        const Comet::AssetType type, const Comet::AssetRevision revision) {
        if(!handle)
            return Comet::Result<void, Comet::Error>::success(); // 空引用允许保存在场景中。
        if(!database().is_current(handle, revision)) {
            return Comet::Result<void, Comet::Error>::failure(
                {"Asset reference revision is stale"});
        }
        return m_manager.ensure_loaded(handle, type);
    }

    Comet::Result<std::size_t, Comet::Error> EditorAssets::prepare_scene(
        Comet::Scene& scene, const Comet::ComponentRegistry& components) {
        return m_scene_assets.prepare_scene(scene, components);
    }

    void EditorAssets::track_scene(
        Comet::Scene& scene, const Comet::ComponentRegistry& components) {
        m_scene_assets.track_scene(scene, components);
    }

    Comet::Result<std::size_t, Comet::Error> EditorAssets::restore_references(
        const Comet::AssetCompletionBudget budget) {
        return m_scene_assets.restore(budget);
    }

}
