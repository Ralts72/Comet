#pragma once

#include "asset/asset_manager.h"
#include "asset/source_monitor.h"
#include "assets/asset_edit.h"
#include "scene/component_registry.h"
#include <set>
#include <unordered_set>
#include <unordered_map>

namespace Comet {
    class Scene;
    class ComponentRegistry;
}

namespace CometEditor {
    class EditorAssets {
    public:
        EditorAssets(Comet::ProjectPaths paths, Comet::AssetRegistry& registry,
            Comet::RenderResourceFactory& factory, Comet::TaskScheduler& scheduler);

        [[nodiscard]] Comet::AssetScanReport refresh();
        [[nodiscard]] Comet::Result<std::optional<Comet::AssetScanReport>, Comet::Error> update();
        [[nodiscard]] Comet::AssetScanReport move(
            Comet::AssetHandle handle, const std::filesystem::path& destination);
        [[nodiscard]] Comet::AssetScanReport import_files(
            std::span<const std::filesystem::path> sources, const std::filesystem::path& directory);
        [[nodiscard]] Comet::Result<void, Comet::Error> apply_edit(const AssetEdit& edit);
        [[nodiscard]] Comet::Result<void, Comet::Error> load_reference(
            Comet::AssetHandle handle, Comet::AssetType type, Comet::AssetRevision revision);
        [[nodiscard]] Comet::Result<std::size_t, Comet::Error> prepare_scene(
            Comet::Scene& scene, const Comet::ComponentRegistry& components);
        void track_scene(Comet::Scene& scene, const Comet::ComponentRegistry& components);
        [[nodiscard]] Comet::Result<std::size_t, Comet::Error> restore_references(
            Comet::AssetManager::CompletionBudget budget = {});
        void request_mesh_reimport(Comet::AssetHandle handle);
        [[nodiscard]] const Comet::AssetDatabase& database() const {
            return m_manager.get_database();
        }

    private:
        void observe(const Comet::AssetSourceMonitor::PollResult& result);
        void accept_scan(const Comet::AssetScanReport& report);
        void acknowledge(const std::filesystem::path& path);

        Comet::AssetManager m_manager;
        Comet::AssetSourceMonitor m_monitor;
        std::unordered_map<Comet::AssetHandle, Comet::AssetManager::MeshImportMode>
            m_pending_mesh_imports;
        std::string m_monitor_error;
        std::unordered_set<Comet::AssetHandle> m_reference_changes;
        std::set<Comet::ComponentRegistry::AssetReference> m_scene_references;
        std::set<Comet::ComponentRegistry::AssetReference> m_pending_references;
        std::set<Comet::ComponentRegistry::AssetReference> m_unresolved_references;
    };
}
