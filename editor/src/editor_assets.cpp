#include "editor_assets.h"
#include "diagnostics/logger.h"
#include "scene/component_registry.h"
#include <utility>

namespace CometEditor {
    EditorAssets::EditorAssets(Comet::ProjectPaths paths, Comet::AssetRegistry& registry,
        Comet::RenderResourceFactory& factory, Comet::TaskScheduler& scheduler)
        : m_manager(paths, registry, factory, scheduler), m_monitor(paths.assets()) {}

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
        if(report.snapshot_updated) {
            m_reference_refresh_requested = true;
            // 失败的模型可能尚无 buffer 依赖索引，不能只检查变化的 Handle。
            m_pending_mesh_imports.clear();
            for(const auto& record : database().get_assets()) {
                if(record.type == Comet::AssetType::Mesh)
                    m_pending_mesh_imports.insert(record.handle);
            }
        }
        if(report.generated_metadata) {
            for(const auto handle : report.added_assets) {
                if(const auto* record = database().find(handle))
                    acknowledge(Comet::metadata_path(record->path));
            }
        }
        for(const auto& issue : report.issues)
            LOG_WARN("Asset scan issue at '{}': {}", issue.path.generic_string(),
                issue.message);
    }

    Comet::AssetScanReport EditorAssets::refresh() {
        observe(m_monitor.poll_now());
        auto report = m_manager.scan();
        accept_scan(report);
        return report;
    }

    std::optional<Comet::AssetScanReport> EditorAssets::update() {
        const auto result = m_monitor.poll();
        observe(result);
        std::optional<Comet::AssetScanReport> report;
        if(result.state == Comet::AssetSourceMonitor::PollState::Changed) {
            report = m_manager.scan();
            accept_scan(*report);
        }
        for(const auto handle : m_pending_mesh_imports) {
            if(!m_manager.import_mesh_async(handle))
                LOG_WARN("Automatic mesh import was not accepted for handle {}",
                    handle.value());
        }
        m_pending_mesh_imports.clear();
        if(!m_manager.process_completions().empty())
            m_reference_refresh_requested = true;
        return report;
    }

    Comet::AssetScanReport EditorAssets::move(
        const Comet::AssetHandle handle, const std::filesystem::path& destination) {
        const auto* previous = database().find(handle);
        const auto old_path = previous ? previous->path : std::filesystem::path{};
        auto report = m_manager.move_asset(handle, destination);
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

    Comet::AssetScanReport EditorAssets::import_files(
        const std::span<const std::filesystem::path> sources,
        const std::filesystem::path& directory) {
        auto report = m_manager.import_files(sources, directory);
        if(report.snapshot_updated) {
            observe(m_monitor.poll_now());
            LOG_INFO("Imported {} asset(s) into assets/{}", report.added_assets.size(),
                directory.generic_string());
        }
        accept_scan(report);
        return report;
    }

    bool EditorAssets::update_material(
        const Comet::AssetHandle handle, const Comet::MaterialData& data) {
        const auto* record = database().find(handle);
        const auto path = record ? record->path : std::filesystem::path{};
        if(!m_manager.update_material(handle, data))
            return false;
        acknowledge(path);
        return true;
    }

    bool EditorAssets::reimport_texture(
        const Comet::AssetHandle handle, const Comet::TextureImportSettings settings) {
        const auto* record = database().find(handle);
        const auto path = record ? record->path : std::filesystem::path{};
        if(!m_manager.reimport_texture(handle, settings))
            return false;
        m_reference_refresh_requested = true;
        if(!path.empty())
            acknowledge(Comet::metadata_path(path));
        return true;
    }

    void EditorAssets::request_mesh_reimport(const Comet::AssetHandle handle) {
        m_pending_mesh_imports.erase(handle);
        if(!m_manager.import_mesh_async(
               handle, Comet::AssetManager::MeshImportMode::Force))
            LOG_WARN(
                "Mesh reimport request was not accepted for handle {}", handle.value());
    }

    bool EditorAssets::load_reference(const Comet::AssetHandle handle,
        const Comet::AssetType type, const Comet::AssetRevision revision) {
        if(!handle)
            return true; // 空引用允许保存在场景中。
        if(!database().is_current(handle, revision)) {
            LOG_ERROR(
                "Cannot load asset {}: stale or incompatible reference", handle.value());
            return false;
        }
        return m_manager.ensure_loaded(handle, type);
    }

    std::size_t EditorAssets::prepare_scene(
        Comet::Scene& scene, const Comet::ComponentRegistry& components) {
        m_reference_refresh_requested = false;
        std::size_t missing = 0;
        for(const auto& reference : components.collect_asset_references(scene)) {
            if(!m_manager.ensure_loaded(reference.handle, reference.type))
                ++missing;
        }
        if(missing)
            LOG_WARN(
                "Scene has {} unresolved asset references; data is preserved for repair",
                missing);
        return missing;
    }

    bool EditorAssets::take_reference_refresh_request() {
        return std::exchange(m_reference_refresh_requested, false);
    }

}
