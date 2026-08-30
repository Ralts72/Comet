#pragma once

#include "asset/database.h"
#include "common/export.h"
#include "core/project_paths.h"

#include <functional>
#include <memory>

namespace Comet {
    class AssetRegistry;
    class Material;
    class Mesh;
    class RenderResourceFactory;
    class TaskScheduler;
    class Texture;
    struct MaterialData;

    class COMET_API AssetManager final {
    public:
        AssetManager(
            ProjectPaths paths,
            AssetRegistry& registry,
            RenderResourceFactory& resource_factory,
            TaskScheduler& task_scheduler);
        ~AssetManager();

        [[nodiscard]] AssetScanReport scan();
        void process_completions();
        [[nodiscard]] std::shared_ptr<Mesh> load_mesh(AssetHandle handle);
        [[nodiscard]] std::shared_ptr<Texture> load_texture(AssetHandle handle);
        [[nodiscard]] std::shared_ptr<Texture> reimport_texture(
            AssetHandle handle,
            TextureImportSettings import_settings);
        [[nodiscard]] std::shared_ptr<Material> load_material(AssetHandle handle);
        [[nodiscard]] std::shared_ptr<Material> update_material(
            AssetHandle handle,
            const MaterialData& data);
        [[nodiscard]] std::shared_ptr<Material> reload_material(AssetHandle handle);

        [[nodiscard]] const AssetDatabase& get_database() const noexcept {
            return m_database;
        }

    private:
        struct AsyncState;

        [[nodiscard]] std::shared_ptr<Mesh> create_runtime_mesh(
            const AssetRecord& record);
        void record_import_dependencies(
            AssetHandle handle,
            const std::vector<std::filesystem::path>& dependencies);
        [[nodiscard]] bool schedule_loaded_mesh_refresh(
            const AssetRecord& record);
        [[nodiscard]] bool schedule_loaded_texture_refresh(
            const AssetRecord& record);
        [[nodiscard]] bool schedule_refresh_task(
            AssetHandle handle,
            AssetRevision revision,
            AssetType type,
            std::function<void()> task);
        void reschedule_after_input_change(
            AssetHandle handle,
            AssetRevision rejected_revision,
            const std::filesystem::path& relative_path,
            AssetType type,
            const std::vector<std::filesystem::path>& dependencies);
        [[nodiscard]] std::shared_ptr<Texture> create_runtime_texture(
            const AssetRecord& record,
            const TextureImportSettings& import_settings);
        [[nodiscard]] std::shared_ptr<Material> create_runtime_material(
            const AssetRecord& record);
        [[nodiscard]] std::shared_ptr<Material> create_runtime_material(
            const AssetRecord& record,
            const MaterialData& data);

        ProjectPaths m_paths;
        AssetDatabase m_database;
        AssetRegistry& m_registry;
        RenderResourceFactory& m_resource_factory;
        TaskScheduler& m_task_scheduler;
        std::shared_ptr<AsyncState> m_async_state;
    };
}
