#pragma once

#include "asset/database.h"
#include "common/export.h"
#include "core/project_paths.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>

namespace Comet {
    class AssetRegistry;
    class ImportService;
    class Material;
    class Mesh;
    class RenderResourceFactory;
    class TaskScheduler;
    class Texture;
    struct MaterialData;

    class COMET_API AssetManager final {
    public:
        struct AsyncLimits {
            std::size_t in_flight = 8;
            std::size_t queued = 128;
        };
        struct AsyncStatus {
            std::size_t in_flight;
            std::size_t queued;
        };
        struct CompletionBudget {
            std::size_t max_results = 2;
            std::chrono::nanoseconds max_time = std::chrono::milliseconds(2);
        };
        enum class MeshImportState {
            Unknown,
            Checking,
            Missing,
            Stale,
            Importing,
            Ready,
            Failed
        };
        AssetManager(ProjectPaths paths, AssetRegistry& registry,
            RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler);
        AssetManager(ProjectPaths paths, AssetRegistry& registry,
            RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler,
            AsyncLimits limits);
        ~AssetManager();

        [[nodiscard]] AssetScanReport scan();
        [[nodiscard]] AssetScanReport move_asset(
            AssetHandle handle, const std::filesystem::path& destination);
        // 本次已发布的导入结果；Mesh Artifact 发布成功不等于 GPU 已驻留。
        std::vector<AssetHandle> process_completions();
        // 过期／失败也计数；单个发布不可抢占，时间预算是软上限。
        std::vector<AssetHandle> process_completions(CompletionBudget budget);
        [[nodiscard]] AsyncStatus get_async_status() const;
        [[nodiscard]] bool ensure_loaded(AssetHandle handle, AssetType expected_type);
        [[nodiscard]] bool import_mesh(AssetHandle handle);
        [[nodiscard]] bool import_mesh_async(AssetHandle handle);
        [[nodiscard]] bool inspect_mesh(AssetHandle handle);
        [[nodiscard]] MeshImportState get_mesh_import_state(AssetHandle handle) const;
        [[nodiscard]] std::shared_ptr<Mesh> load_mesh(AssetHandle handle);
        [[nodiscard]] std::shared_ptr<Texture> load_texture(AssetHandle handle);
        [[nodiscard]] std::shared_ptr<Texture> reimport_texture(
            AssetHandle handle, TextureImportSettings import_settings);
        [[nodiscard]] std::shared_ptr<Material> load_material(AssetHandle handle);
        [[nodiscard]] std::shared_ptr<Material> update_material(
            AssetHandle handle, const MaterialData& data);
        [[nodiscard]] std::shared_ptr<Material> reload_material(AssetHandle handle);

        [[nodiscard]] const AssetDatabase& get_database() const noexcept {
            return m_database;
        }

    private:
        struct AsyncState;
        struct ImportResult;

        void apply_scan_report(const AssetScanReport& report);
        void reload_loaded_material_dependents(AssetHandle texture_handle);
        [[nodiscard]] std::shared_ptr<Mesh> create_runtime_mesh(
            const AssetRecord& record);
        void record_import_dependencies(
            AssetHandle handle, const std::vector<std::filesystem::path>& dependencies);
        [[nodiscard]] bool schedule_loaded_mesh_refresh(const AssetRecord& record);
        [[nodiscard]] bool schedule_mesh_task(
            const AssetRecord& record, bool inspect_only);
        [[nodiscard]] bool schedule_loaded_texture_refresh(const AssetRecord& record);
        [[nodiscard]] bool schedule_refresh_task(AssetHandle handle,
            AssetRevision revision, AssetType type,
            std::function<void(ImportResult&)> task);
        void dispatch_queued_tasks();
        [[nodiscard]] bool publish_import_result(ImportResult& result);
        [[nodiscard]] std::shared_ptr<Texture> create_runtime_texture(
            const AssetRecord& record, const TextureImportSettings& import_settings);
        [[nodiscard]] std::shared_ptr<Material> create_runtime_material(
            const AssetRecord& record);
        [[nodiscard]] std::shared_ptr<Material> create_runtime_material(
            const AssetRecord& record, const MaterialData& data);

        ProjectPaths m_paths;
        AssetDatabase m_database;
        std::unique_ptr<ImportService> m_import_service;
        AssetRegistry& m_registry;
        RenderResourceFactory& m_resource_factory;
        TaskScheduler& m_task_scheduler;
        AsyncLimits m_async_limits;
        std::unique_ptr<AsyncState> m_async_state;
    };
}
