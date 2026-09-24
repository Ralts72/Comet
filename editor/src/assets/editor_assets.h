#pragma once

#include "asset/asset_manager.h"
#include "assets/source_monitor.h"
#include "assets/asset_edit.h"
#include "asset/reference.h"
#include <memory>
#include <optional>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace Comet {
    class Scene;
    class ComponentRegistry;
    class ShaderProgramArtifact;
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
        [[nodiscard]] Comet::AssetScanReport remove(Comet::AssetHandle handle);
        [[nodiscard]] Comet::AssetScanReport import_files(
            std::span<const std::filesystem::path> sources, const std::filesystem::path& directory);
        [[nodiscard]] Comet::Result<void, Comet::Error> apply_texture_edit(const AssetEdit& edit);
        [[nodiscard]] Comet::Result<Comet::MaterialData> read_material(
            const AssetRead& request) const;
        [[nodiscard]] Comet::Result<Comet::AssetManager::MaterialUpdate, Comet::Error>
        prepare_material_edit(const AssetEdit& edit);
        [[nodiscard]] Comet::Result<void, Comet::Error> commit_material_edit(
            const Comet::AssetManager::MaterialUpdate& update);
        [[nodiscard]] Comet::AssetScanReport create_material(
            const std::filesystem::path& destination, const Comet::MaterialData& data);
        [[nodiscard]] Comet::AssetScanReport create_script(
            const std::filesystem::path& destination);
        [[nodiscard]] Comet::Result<void, Comet::Error> load_reference(
            Comet::AssetHandle handle, Comet::AssetType type, Comet::AssetRevision revision);
        [[nodiscard]] Comet::Result<std::size_t, Comet::Error> prepare_scene(
            Comet::Scene& scene, const Comet::ComponentRegistry& components);
        void track_scene(Comet::Scene& scene, const Comet::ComponentRegistry& components);
        [[nodiscard]] Comet::Result<std::size_t, Comet::Error> restore_references(
            Comet::AssetCompletionBudget budget = {});
        void request_mesh_reimport(Comet::AssetHandle handle);
        [[nodiscard]] const Comet::AssetDatabase& database() const { return m_database; }
        [[nodiscard]] std::shared_ptr<const Comet::ShaderProgramArtifact> compiled_shader_program(
            Comet::AssetHandle handle) const;

    private:
        void observe(const AssetSourceMonitor::PollResult& result);
        void accept_scan(const Comet::AssetScanReport& report);
        void acknowledge(const std::filesystem::path& path);
        void schedule_shader_program_imports();
        Comet::ProjectPaths m_paths;
        Comet::AssetDatabase m_database;
        Comet::AssetManager m_manager;
        AssetSourceMonitor m_monitor;
        std::unordered_set<Comet::AssetHandle> m_pending_shader_programs;
        std::unordered_map<Comet::AssetHandle, Comet::MeshImportMode> m_pending_mesh_imports;
        std::string m_monitor_error;
        std::unordered_set<Comet::AssetHandle> m_reference_changes;
        std::set<Comet::AssetReference> m_scene_references;
        std::set<Comet::AssetReference> m_pending_references;
        std::set<Comet::AssetReference> m_unresolved_references;
    };
}
