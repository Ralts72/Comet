#include "assets/editor_assets.h"
#include "assets/shader_program_import.h"
#include "scene/component_registry.h"
#include "diagnostics/logger.h"
#include <utility>
#include "graphics/result.h"
#include "asset/serialization/material_serializer.h"
#include "asset/source_operations.h"
#include "core/task_scheduler.h"

#include <algorithm>

namespace CometEditor {
    EditorAssets::EditorAssets(Comet::ProjectPaths paths, Comet::AssetRegistry& registry,
        Comet::RenderResourceFactory& factory, Comet::TaskScheduler& scheduler)
        : m_paths(std::move(paths)), m_database(m_paths),
          m_manager(m_database, registry, factory, scheduler), m_monitor(m_paths.assets()) {}

    Comet::Result<Comet::MaterialData> EditorAssets::read_material(const AssetRead& request) const {
        const auto* record = database().find(request.handle);
        if(!record || record->type != Comet::AssetType::Material
            || !database().is_current(request.handle, request.revision))
            return Comet::Result<Comet::MaterialData>::failure("Material read request is stale");
        return Comet::MaterialSerializer{}.load(m_paths.assets() / record->path);
    }

    void EditorAssets::observe(const Comet::AssetSourceMonitor::PollResult& result) {
        using State = Comet::AssetSourceMonitor::PollState;
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

    void EditorAssets::accept_scan(const Comet::AssetScanReport& report) {
        m_manager.accept_scan_report(report);
        if(report.snapshot_updated) {
            std::unordered_set<Comet::AssetHandle> changed;
            changed.insert(report.added_assets.begin(), report.added_assets.end());
            changed.insert(report.modified_assets.begin(), report.modified_assets.end());
            database().include_dependents(changed);
            for(const auto handle : changed) {
                const auto* record = database().find(handle);
                if(record && record->type == Comet::AssetType::ShaderProgram)
                    m_pending_shader_programs.insert(handle);
            }
            for(const auto handle : report.removed_assets)
                m_pending_shader_programs.erase(handle);
            m_reference_changes.insert(report.added_assets.begin(), report.added_assets.end());
            m_reference_changes.insert(
                report.modified_assets.begin(), report.modified_assets.end());
            m_reference_changes.insert(report.removed_assets.begin(), report.removed_assets.end());
            m_pending_references.insert(
                m_unresolved_references.begin(), m_unresolved_references.end());
            // 失败的模型可能尚无 buffer 依赖索引，不能只检查变化的 Handle。
            std::erase_if(m_pending_mesh_imports, [&](const auto& entry) {
                const auto* record = database().find(entry.first);
                return !record || record->type != Comet::AssetType::Mesh;
            });
            for(const auto& record : database().get_assets()) {
                if(record.type == Comet::AssetType::Mesh)
                    m_pending_mesh_imports.try_emplace(
                        record.handle, Comet::MeshImportMode::IfNeeded);
            }
        }
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
        observe(m_monitor.poll_now());
        auto report = m_database.scan();
        accept_scan(report);
        return report;
    }

    Comet::Result<std::optional<Comet::AssetScanReport>, Comet::Error> EditorAssets::update() {
        const auto result = m_monitor.poll();
        observe(result);
        std::optional<Comet::AssetScanReport> report;
        if(result.state == Comet::AssetSourceMonitor::PollState::Changed) {
            report = m_database.scan();
            accept_scan(*report);
        }
        schedule_shader_program_imports();
        for(auto request = m_pending_mesh_imports.begin();
            request != m_pending_mesh_imports.end();) {
            const auto* record = database().find(request->first);
            if(!record || record->type != Comet::AssetType::Mesh) {
                request = m_pending_mesh_imports.erase(request);
                continue;
            }
            if(!m_manager.import_mesh_async(request->first, request->second))
                break;
            request = m_pending_mesh_imports.erase(request);
        }
        auto completed = m_manager.process_completions();
        if(!completed)
            return Comet::Result<std::optional<Comet::AssetScanReport>, Comet::Error>::failure(
                completed.error());
        m_reference_changes.insert(completed.value().begin(), completed.value().end());
        return Comet::Result<std::optional<Comet::AssetScanReport>, Comet::Error>::success(
            std::move(report));
    }

    std::shared_ptr<const Comet::ShaderProgramArtifact> EditorAssets::compiled_shader_program(
        const Comet::AssetHandle handle) const {
        return m_manager.compiled_shader_program(handle);
    }

    void EditorAssets::schedule_shader_program_imports() {
        for(auto pending = m_pending_shader_programs.begin();
            pending != m_pending_shader_programs.end();) {
            const auto handle = *pending;
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
        auto report = Comet::AssetSourceOperations::move(m_database, m_paths, handle, destination);
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
        auto report = Comet::AssetSourceOperations::remove_asset(m_database, m_paths, handle);
        if(report.snapshot_updated) {
            acknowledge(old_path);
            acknowledge(Comet::metadata_path(old_path));
        }
        accept_scan(report);
        return report;
    }

    Comet::AssetScanReport EditorAssets::import_files(
        const std::span<const std::filesystem::path> sources,
        const std::filesystem::path& directory) {
        auto report =
            Comet::AssetSourceOperations::import_files(m_database, m_paths, sources, directory);
        if(report.snapshot_updated) {
            observe(m_monitor.poll_now());
            LOG_INFO("Imported {} asset(s) into assets/{}", report.added_assets.size(),
                directory.generic_string());
        }
        accept_scan(report);
        return report;
    }

    Comet::AssetScanReport EditorAssets::create_material(
        const std::filesystem::path& destination, const Comet::MaterialData& data) {
        auto report =
            Comet::AssetSourceOperations::create_material(m_database, m_paths, destination, data);
        if(report.snapshot_updated) {
            acknowledge(destination);
            acknowledge(Comet::metadata_path(destination));
        }
        accept_scan(report);
        return report;
    }

    Comet::AssetScanReport EditorAssets::create_script(const std::filesystem::path& destination) {
        auto report = Comet::AssetSourceOperations::create_script(m_database, m_paths, destination);
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
        m_reference_changes.insert(edit.handle);
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
        return m_manager.prepare_references(components.collect_asset_references(scene),
            Comet::AssetManager::MissingAssetPolicy::AllowMissing);
    }

    void EditorAssets::track_scene(
        Comet::Scene& scene, const Comet::ComponentRegistry& components) {
        const auto references = components.collect_asset_references(scene);
        std::set<Comet::AssetReference> next(references.begin(), references.end());
        for(const auto& reference : next)
            if(!m_scene_references.contains(reference))
                m_pending_references.insert(reference);
        m_scene_references = std::move(next);
        std::erase_if(m_pending_references,
            [&](const auto& reference) { return !m_scene_references.contains(reference); });
        std::erase_if(m_unresolved_references,
            [&](const auto& reference) { return !m_scene_references.contains(reference); });
    }

    Comet::Result<std::size_t, Comet::Error> EditorAssets::restore_references(
        const Comet::AssetCompletionBudget budget) {
        auto changed = std::exchange(m_reference_changes, {});
        if(!changed.empty()) {
            database().include_dependents(changed);
            for(const auto& reference : m_scene_references)
                if(changed.contains(reference.handle))
                    m_pending_references.insert(reference);
            // 失败的材质可能尚无完整依赖索引，发布事件也重试有限的未解析引用。
            m_pending_references.insert(
                m_unresolved_references.begin(), m_unresolved_references.end());
        }
        const auto start = std::chrono::steady_clock::now();
        std::size_t processed = 0;
        while(!m_pending_references.empty() && processed < budget.max_results
              && budget.max_time > std::chrono::nanoseconds::zero()
              && (processed == 0 || std::chrono::steady_clock::now() - start < budget.max_time)) {
            const auto reference = *m_pending_references.begin();
            m_pending_references.erase(m_pending_references.begin());
            ++processed;
            auto loaded = m_manager.request_load(reference.handle, reference.type);
            if(loaded) {
                m_unresolved_references.erase(reference);
            } else {
                m_unresolved_references.insert(reference);
                if(Comet::is_device_lost(loaded.error()))
                    return Comet::Result<std::size_t, Comet::Error>::failure(loaded.error());
                LOG_WARN(
                    "Unresolved asset {}: {}", reference.handle.value(), loaded.error().message);
            }
        }
        return Comet::Result<std::size_t, Comet::Error>::success(processed);
    }

}
