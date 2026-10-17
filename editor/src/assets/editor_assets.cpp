#include "asset/runtime/prefab_loader.h"
#include "assets/editor_assets.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "asset/default_assets.h"
#include "asset/shader_program_import.h"
#include "assets/system_trash.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "asset/serialization/material_serializer.h"
#include "assets/source_operations.h"
#include "core/task_scheduler.h"
#include "asset/script.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace CometEditor {
    EditorAssets::EditorAssets(Comet::ProjectPaths paths, Comet::AssetRegistry& registry,
        Comet::RenderResourceFactory& factory, Comet::TaskScheduler& scheduler,
        const std::chrono::milliseconds quiet_period, const Comet::AssetImportLimits limits,
        AssetSourceOperations::TrashMover trash_mover)
        : m_limits(limits), m_trash_mover(std::move(trash_mover)), m_database(std::move(paths)),
          m_manager(m_database, registry, factory, scheduler, m_limits),
          m_scene_assets(m_database, m_manager), m_thumbnails(m_database, scheduler),
          m_monitor(m_database.paths().assets()), m_scheduler(scheduler),
          m_quiet_period(quiet_period) {
        if(!m_trash_mover)
            m_trash_mover = SystemTrash::move;
        std::error_code error;
        const auto pending = m_database.paths().local_data() / "pending-deletions";
        if(std::filesystem::is_directory(pending, error)
            && !std::filesystem::is_empty(pending, error))
            LOG_WARN("Unfinished asset deletion remains at '{}'; inspect it before cleanup",
                pending.string());
        if(!m_monitor.uses_native_notifications())
            LOG_WARN("Asset source monitor is using periodic fallback scans");
    }

    Comet::Result<Comet::TerrainData> EditorAssets::read_terrain(
        const Comet::AssetTarget& target) const {
        const auto* record = m_database.find(target.handle);
        if(!record || record->path.extension() != ".terrain" || !m_database.is_current(target))
            return Comet::Result<Comet::TerrainData>::failure("Terrain read is stale");
        return Comet::TerrainData::load(m_database.paths().assets() / record->path);
    }
    Comet::AssetScanReport EditorAssets::create_terrain(const std::filesystem::path& destination) {
        auto report = AssetSourceOperations::create_terrain(m_database, destination, {});
        accept_scan(report);
        if(report.succeeded())
            acknowledge(destination);
        return report;
    }
    Comet::Result<void, Comet::Error> EditorAssets::apply_terrain_edit(const AssetEdit& edit) {
        const auto* terrain = std::get_if<TerrainEdit>(&edit.value);
        if(!terrain || edit.action != AssetEdit::Action::Apply)
            return Comet::Result<void, Comet::Error>::failure({"Invalid terrain edit"});
        const auto* record = m_database.find(edit.target.handle);
        if(!record)
            return Comet::Result<void, Comet::Error>::failure({"Terrain is no longer indexed"});
        const auto path = record->path;
        auto report = AssetSourceOperations::save_terrain(m_database, edit.target, terrain->after);
        accept_scan(report);
        if(!report.succeeded())
            return Comet::Result<void, Comet::Error>::failure({report.issues.front().message});
        acknowledge(path);
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<std::shared_ptr<const Comet::ModelData>, Comet::Error> EditorAssets::
        rebuild_terrain(Comet::AssetHandle asset) {
        using Built = Comet::Result<std::shared_ptr<const Comet::ModelData>, Comet::Error>;
        const auto* record = m_database.find(asset);
        if(!record || record->path.extension() != ".terrain")
            return Built::failure({"Expected a terrain source"});
        if(auto imported = m_manager.import_model(asset); !imported)
            return Built::failure(imported.error());
        accept_scan(m_manager.scan());
        m_pending_source_imports.erase(asset);
        return m_manager.load_model(asset);
    }

    Comet::Result<Comet::GameData> EditorAssets::read_data(const Comet::AssetTarget& target) const {
        const auto* record = m_database.find(target.handle);
        if(!record || record->type != Comet::AssetType::Data || !m_database.is_current(target))
            return Comet::Result<Comet::GameData>::failure("Data read is stale");
        return Comet::GameData::load(m_database.paths().assets() / record->path);
    }

    Comet::Result<Comet::AnimationLibrary> EditorAssets::read_animation_library(
        const Comet::AssetTarget& target) const {
        const auto* record = m_database.find(target.handle);
        if(!record || record->type != Comet::AssetType::AnimationLibrary
            || !m_database.is_current(target))
            return Comet::Result<Comet::AnimationLibrary>::failure(
                "Animation library read is stale");
        return Comet::AnimationLibrary::load(m_database.paths().assets() / record->path);
    }
    Comet::Result<Comet::AnimationController> EditorAssets::read_animation_controller(
        const Comet::AssetTarget& target) const {
        const auto* record = m_database.find(target.handle);
        if(!record || record->type != Comet::AssetType::AnimationController
            || !m_database.is_current(target))
            return Comet::Result<Comet::AnimationController>::failure(
                "Animation controller read is stale");
        return Comet::AnimationControllerSerializer{}.load(
            m_database.paths().assets() / record->path);
    }
    Comet::AssetScanReport EditorAssets::create_animation_controller(
        const std::filesystem::path& destination) {
        auto data = Comet::AnimationControllerSerializer{}.load(
            Comet::default_character_source().parent_path() / "default.ani");
        if(!data) {
            Comet::AssetScanReport report;
            report.issues.push_back({destination, data.error()});
            return report;
        }
        auto report = AssetSourceOperations::create_animation_controller(
            m_database, destination, data.value());
        accept_scan(report);
        if(report.succeeded())
            acknowledge(destination);
        return report;
    }
    Comet::Result<void, Comet::Error> EditorAssets::apply_animation_edit(const AssetEdit& edit) {
        const auto* controller = std::get_if<AnimationControllerEdit>(&edit.value);
        const auto* library = std::get_if<AnimationLibraryEdit>(&edit.value);
        const auto* record = m_database.find(edit.target.handle);
        if((!controller && !library) || edit.action != AssetEdit::Action::Apply || !record
            || !m_database.is_current(edit.target))
            return Comet::Result<void, Comet::Error>::failure(
                {"Animation asset edit is stale or invalid"});
        const auto path = record->path;
        Comet::AssetScanReport report;
        auto type = Comet::AssetType::AnimationController;
        if(controller)
            report = AssetSourceOperations::save_animation_controller(
                m_database, edit.target, controller->after);
        else {
            type = Comet::AssetType::AnimationLibrary;
            report = AssetSourceOperations::save_animation_library(
                m_database, edit.target, library->after);
        }
        accept_scan(report);
        if(!report.succeeded())
            return Comet::Result<void, Comet::Error>::failure({report.issues.front().message});
        acknowledge(path);
        return m_manager.ensure_loaded(edit.target.handle, type);
    }

    Comet::Result<Comet::MaterialData> EditorAssets::read_material(
        const Comet::AssetTarget& request) const {
        const auto* record = database().find(request.handle);
        if(!record || record->type != Comet::AssetType::Material || !database().is_current(request))
            return Comet::Result<Comet::MaterialData>::failure("Material read request is stale");
        return Comet::MaterialSerializer{}.load(m_database.paths().assets() / record->path);
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
                queue_source_import(*record);
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
            std::erase_if(m_pending_source_imports, [&](const auto& entry) {
                const auto* record = database().find(entry.first);
                return !record || record->source_asset
                       || (record->type != Comet::AssetType::Mesh
                           && record->type != Comet::AssetType::Model);
            });
            // 首次导入失败时依赖可能尚未入库，后续文件变化仍需重试它。
            for(const auto handle : m_manager.source_imports_needing_recheck())
                m_pending_source_imports.try_emplace(handle, Comet::AssetImportMode::IfNeeded);
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
                queue_source_import(record);
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
        schedule_source_imports();
        auto completed = m_manager.process_completions();
        if(!completed)
            return Comet::Result<std::optional<Comet::AssetScanReport>, Comet::Error>::failure(
                completed.error());
        for(const auto handle : completed.value()) {
            m_thumbnails.invalidate(handle);
            if(const auto* record = database().find(handle);
                record && record->type == Comet::AssetType::Model)
                acknowledge(Comet::metadata_path(record->path));
        }
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
                [paths = m_database.paths(), sources = request.sources,
                    directory = request.directory, limits = m_limits] {
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
            auto completion =
                m_scheduler.try_submit_result([paths = m_database.paths(), database_generation] {
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

    void EditorAssets::queue_source_import(
        const Comet::AssetRecord& record, Comet::AssetImportMode mode) {
        if(!record.source_asset && record.type != Comet::AssetType::Mesh
            && record.type != Comet::AssetType::Model)
            return;
        const auto source = record.source_asset ? record.source_asset : record.handle;
        if(mode == Comet::AssetImportMode::Force)
            m_pending_source_imports[source] = mode;
        else
            m_pending_source_imports.try_emplace(source, mode);
    }

    void EditorAssets::schedule_source_imports() {
        for(auto request = m_pending_source_imports.begin();
            request != m_pending_source_imports.end();) {
            const auto* record = database().find(request->first);
            if(!record
                || (record->type != Comet::AssetType::Mesh
                    && record->type != Comet::AssetType::Model)) {
                request = m_pending_source_imports.erase(request);
                continue;
            }
            if(m_manager.is_import_pending(request->first)) {
                ++request;
                continue;
            }
            const bool accepted =
                record->type == Comet::AssetType::Model
                    ? m_manager.import_model_async(request->first, request->second)
                    : m_manager.import_mesh_async(request->first, request->second);
            if(!accepted)
                break;
            request = m_pending_source_imports.erase(request);
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
            auto request = Comet::ShaderProgramImport::resolve(database(), handle);
            if(!request) {
                LOG_ERROR("Shader program {}: {}", handle.value(), request.error());
                m_manager.invalidate_shader_program(handle);
                pending = m_pending_shader_programs.erase(pending);
                continue;
            }
            const auto input = std::move(request).value();
            if(!m_manager.import_shader_program_async(input, [paths = m_database.paths(), input] {
                   return Comet::ShaderProgramImport::prepare(paths, input);
               }))
                break;
            pending = m_pending_shader_programs.erase(pending);
        }
    }

    Comet::AssetScanReport EditorAssets::move(
        const Comet::AssetHandle handle, const std::filesystem::path& destination) {
        const auto* previous = database().find(handle);
        const auto old_path = previous ? previous->path : std::filesystem::path{};
        auto report = AssetSourceOperations::move(m_database, handle, destination);
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
        auto report = AssetSourceOperations::remove_asset(m_database, handle, m_trash_mover);
        if(report.snapshot_updated) {
            acknowledge(old_path);
            acknowledge(Comet::metadata_path(old_path));
        }
        accept_scan(report);
        return report;
    }

    Comet::AssetScanReport EditorAssets::remove_module(const std::filesystem::path& source) {
        auto report = AssetSourceOperations::remove_module(m_database, source, m_trash_mover);
        if(report.snapshot_updated)
            acknowledge(source);
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

    Comet::AssetScanReport EditorAssets::create_prefab(const std::filesystem::path& destination,
        const Comet::Prefab& prefab, const Comet::ComponentRegistry& registry) {
        auto report =
            AssetSourceOperations::create_prefab(m_database, destination, prefab, registry);
        if(report.snapshot_updated) {
            acknowledge(destination);
            acknowledge(Comet::metadata_path(destination));
        }
        accept_scan(report);
        return report;
    }

    Comet::Result<void> EditorAssets::save_prefab(const Comet::AssetTarget& target,
        const Comet::Prefab& prefab, const Comet::ComponentRegistry& registry) {
        auto report = AssetSourceOperations::save_prefab(m_database, target, prefab, registry);
        if(const auto* record = m_database.find(target.handle))
            acknowledge(record->path);
        accept_scan(report);
        if(!report.succeeded() || !report.snapshot_updated)
            return Comet::Result<void>::failure(
                report.issues.empty() ? "Cannot save prefab" : report.issues.front().message);
        return Comet::Result<void>::success();
    }

    Comet::Result<Comet::Prefab> EditorAssets::load_prefab(
        const Comet::AssetTarget& target, const Comet::ComponentRegistry& registry) {
        const auto* record = m_database.find(target.handle);
        if(!record || !m_database.is_current(target))
            return Comet::Result<Comet::Prefab>::failure("Prefab identity is stale or invalid");
        if(record->type == Comet::AssetType::Prefab)
            return Comet::read_prefab_content(m_database, target.handle, registry);
        if(record->type != Comet::AssetType::Model)
            return Comet::Result<Comet::Prefab>::failure("Asset is not a prefab or model source");
        const auto name = record->path.stem().string();
        auto model = m_manager.load_model(target.handle);
        if(!model)
            return Comet::Result<Comet::Prefab>::failure(model.error().message);
        return Comet::Prefab::from_model(*model.value(), target.handle, registry, name);
    }

    Comet::AssetScanReport EditorAssets::create_material(
        const std::filesystem::path& destination, const Comet::MaterialData& data) {
        auto report = AssetSourceOperations::create_material(m_database, destination, data);
        if(report.snapshot_updated) {
            acknowledge(destination);
            acknowledge(Comet::metadata_path(destination));
        }
        accept_scan(report);
        return report;
    }

    Comet::AssetScanReport EditorAssets::create_environment(
        const std::filesystem::path& destination, const Comet::TextureData& cubemap) {
        auto report = AssetSourceOperations::create_environment(m_database, destination, cubemap);
        if(report.snapshot_updated) {
            acknowledge(destination);
            acknowledge(Comet::metadata_path(destination));
        }
        accept_scan(report);
        return report;
    }

    Comet::AssetScanReport EditorAssets::create_irradiance(
        const std::filesystem::path& destination, const Comet::IrradianceData& data) {
        auto report = AssetSourceOperations::create_irradiance(m_database, destination, data);
        if(report.snapshot_updated) {
            acknowledge(destination);
            acknowledge(Comet::metadata_path(destination));
        }
        accept_scan(report);
        return report;
    }

    Comet::AssetScanReport EditorAssets::create_navigation(
        const std::filesystem::path& destination, const Comet::NavigationData& data) {
        auto report = AssetSourceOperations::create_navigation(m_database, destination, data);
        if(report.snapshot_updated) {
            acknowledge(destination);
            acknowledge(Comet::metadata_path(destination));
        }
        accept_scan(report);
        return report;
    }

    Comet::AssetScanReport EditorAssets::create_script(
        const std::filesystem::path& destination, const AssetSourceOperations::ScriptKind kind) {
        auto report = AssetSourceOperations::create_script(m_database, destination, kind);
        if(report.snapshot_updated) {
            acknowledge(destination);
            if(kind == AssetSourceOperations::ScriptKind::Component)
                acknowledge(Comet::metadata_path(destination));
        }
        accept_scan(report);
        return report;
    }

    Comet::AssetScanReport EditorAssets::move_module(
        const std::filesystem::path& source, const std::filesystem::path& destination) {
        auto report = AssetSourceOperations::move_module(m_database, source, destination);
        if(report.snapshot_updated) {
            acknowledge(source);
            acknowledge(destination);
            LOG_INFO(
                "Moved Lua module from require(\"{}\") to require(\"{}\"); update Lua require calls manually",
                Comet::Script::module_name(source).value(),
                Comet::Script::module_name(destination).value());
        }
        accept_scan(report);
        return report;
    }

    Comet::Result<Comet::AssetManager::MaterialUpdate, Comet::Error> EditorAssets::
        prepare_material_edit(const AssetEdit& edit) {
        using Preparation = Comet::Result<Comet::AssetManager::MaterialUpdate, Comet::Error>;
        if(!database().is_current(edit.target))
            return Preparation::failure({"Asset edit revision is stale"});
        const auto* material = std::get_if<MaterialEdit>(&edit.value);
        if(!material)
            return Preparation::failure({"Asset edit is not a material edit"});
        return m_manager.prepare_material_update(edit.target.handle, material->after);
    }

    Comet::Result<void, Comet::Error> EditorAssets::commit_material_edit(
        const Comet::AssetManager::MaterialUpdate& update) {
        auto committed = m_manager.commit_material_update(update);
        if(!committed)
            return Comet::Result<void, Comet::Error>::failure(committed.error());
        acknowledge(database().find(update.handle())->path);
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<void, Comet::Error> EditorAssets::preview_material_edit(
        Comet::AssetManager::MaterialUpdate& update) {
        return m_manager.preview_material_update(update);
    }

    Comet::Result<void, Comet::Error> EditorAssets::restore_material_preview(
        const Comet::AssetManager::MaterialUpdate& update,
        const std::shared_ptr<Comet::Material>& previous) {
        return m_manager.restore_material_preview(update, previous);
    }

    Comet::Result<void, Comet::Error> EditorAssets::apply_texture_edit(const AssetEdit& edit) {
        if(!database().is_current(edit.target))
            return Comet::Result<void, Comet::Error>::failure({"Asset edit revision is stale"});

        const auto* texture = std::get_if<TextureEdit>(&edit.value);
        if(!texture)
            return Comet::Result<void, Comet::Error>::failure({"Asset edit is not a texture edit"});
        const auto path = database().find(edit.target.handle)->path;
        if(auto imported = m_manager.reimport_texture(edit.target.handle, texture->after);
            !imported)
            return Comet::Result<void, Comet::Error>::failure(imported.error());
        m_scene_assets.mark_changed(edit.target.handle);
        acknowledge(Comet::metadata_path(path));
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<void, Comet::Error> EditorAssets::apply_audio_edit(const AssetEdit& edit) {
        if(!database().is_current(edit.target))
            return Comet::Result<void, Comet::Error>::failure({"Asset edit revision is stale"});
        const auto* audio = std::get_if<AudioEdit>(&edit.value);
        if(!audio)
            return Comet::Result<void, Comet::Error>::failure({"Asset edit is not an audio edit"});
        const auto path = database().find(edit.target.handle)->path;
        if(auto imported = m_manager.reimport_audio(edit.target.handle, audio->after); !imported)
            return Comet::Result<void, Comet::Error>::failure(imported.error());
        m_scene_assets.mark_changed(edit.target.handle);
        acknowledge(Comet::metadata_path(path));
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<std::shared_ptr<const Comet::ModelData>, Comet::Error> EditorAssets::load_model(
        const Comet::AssetTarget& target) {
        if(!database().is_current(target))
            return Comet::Result<std::shared_ptr<const Comet::ModelData>, Comet::Error>::failure(
                {"Model insertion request is stale"});
        return m_manager.load_model(target.handle);
    }

    void EditorAssets::request_source_reimport(const Comet::AssetHandle handle) {
        const auto* record = database().find(handle);
        if(!record)
            return;
        queue_source_import(*record, Comet::AssetImportMode::Force);
        schedule_source_imports();
    }

    Comet::Result<void, Comet::Error> EditorAssets::load_reference(const Comet::AssetHandle handle,
        const Comet::AssetType type, const Comet::AssetRevision revision) {
        if(!handle)
            return Comet::Result<void, Comet::Error>::success(); // 空引用允许保存在场景中。
        if(const auto* asset = Comet::find_default_asset(handle);
            asset && asset->type == type && revision == Comet::INVALID_ASSET_REVISION)
            return m_manager.ensure_loaded(handle, type);
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

    Comet::Result<std::vector<Comet::AssetReference>, Comet::Error> EditorAssets::
        capture_play_references(Comet::Scene& scene, const Comet::ComponentRegistry& components) {
        return m_manager.collect_scene_references(scene, components);
    }

    Comet::Result<Comet::AssetManager::ResidencyRelease, Comet::Error> EditorAssets::release_unused(
        Comet::Scene& active, Comet::Scene* retained_edit,
        const Comet::ComponentRegistry& components,
        const std::span<const Comet::AssetHandle> preview_roots,
        const std::span<const Comet::AssetReference> entry_references) {
        using Released = Comet::Result<Comet::AssetManager::ResidencyRelease, Comet::Error>;
        m_scene_assets.track_scene(active, components);
        auto references = m_manager.collect_scene_references(active, components);
        if(!references)
            return Released::failure(references.error());
        references.value().insert(
            references.value().end(), entry_references.begin(), entry_references.end());
        if(retained_edit && retained_edit != &active) {
            auto baseline = m_manager.collect_scene_references(*retained_edit, components);
            if(!baseline)
                return Released::failure(baseline.error());
            references.value().insert(
                references.value().end(), baseline.value().begin(), baseline.value().end());
        }
        return Released::success(m_manager.release_unused(references.value(), preview_roots));
    }

    Comet::Result<std::size_t, Comet::Error> EditorAssets::restore_references(
        const Comet::AssetCompletionBudget budget) {
        return m_scene_assets.restore(budget);
    }

}
