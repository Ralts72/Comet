#pragma once

#include "asset/database.h"
#include "common/export.h"
#include "common/error.h"
#include "common/result.h"
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
    struct MeshData;
    struct GraphicsError;
    class AssetTaskQueue;
    struct AssetImportResult;

    class COMET_API AssetManager final {
    public:
        struct AsyncLimits {
            // 两项均必须为正；这是内部调度预算，不是禁用异步加载的开关。
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
        enum class MeshImportMode { IfNeeded, Force };
        AssetManager(ProjectPaths paths, AssetRegistry& registry,
            RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler);
        AssetManager(ProjectPaths paths, AssetRegistry& registry,
            RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler,
            AsyncLimits limits);
        ~AssetManager();

        [[nodiscard]] AssetScanReport scan();
        [[nodiscard]] AssetScanReport move_asset(
            AssetHandle handle, const std::filesystem::path& destination);
        [[nodiscard]] AssetScanReport import_files(
            std::span<const std::filesystem::path> sources, const std::filesystem::path& directory);
        // 本次成功发布的结果；Mesh Artifact 发布不代表 GPU 已驻留。
        [[nodiscard]] Result<std::vector<AssetHandle>, Error> process_completions();
        // 失败／过期也计数；时间预算不抢占单个发布操作。
        [[nodiscard]] Result<std::vector<AssetHandle>, Error> process_completions(
            CompletionBudget budget);
        [[nodiscard]] AsyncStatus get_async_status() const;
        [[nodiscard]] Result<void, Error> ensure_loaded(
            AssetHandle handle, AssetType expected_type);
        [[nodiscard]] Result<void, Error> import_mesh(AssetHandle handle);
        [[nodiscard]] bool import_mesh_async(
            AssetHandle handle, MeshImportMode mode = MeshImportMode::IfNeeded);
        [[nodiscard]] Result<std::shared_ptr<Mesh>, Error> load_mesh(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Texture>, Error> load_texture(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Texture>, Error> reimport_texture(
            AssetHandle handle, TextureImportSettings import_settings);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> load_material(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> update_material(
            AssetHandle handle, const MaterialData& data);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> reload_material(AssetHandle handle);

        [[nodiscard]] const AssetDatabase& get_database() const noexcept { return m_database; }

    private:
        void apply_scan_report(const AssetScanReport& report);
        Result<void, Error> reload_loaded_material_dependents(AssetHandle texture_handle);
        Result<void, Error> publish_material(AssetHandle handle, const MaterialData& data,
            const std::shared_ptr<Material>& material, bool replace_existing);
        [[nodiscard]] Result<std::shared_ptr<Mesh>, Error> create_runtime_mesh(
            const AssetRecord& record);
        Result<void, GraphicsError> refresh_loaded_mesh(
            AssetHandle handle, AssetRevision revision, const MeshData& data);
        void record_import_dependencies(
            AssetHandle handle, const std::vector<std::filesystem::path>& dependencies);
        [[nodiscard]] bool schedule_mesh_task(const AssetRecord& record, MeshImportMode mode);
        [[nodiscard]] bool schedule_loaded_texture_refresh(const AssetRecord& record);
        [[nodiscard]] bool schedule_material_refresh(const AssetRecord& record);
        Result<void, Error> publish_import_result(
            AssetImportResult& result, std::vector<AssetHandle>& published);
        [[nodiscard]] Result<std::shared_ptr<Texture>, Error> create_runtime_texture(
            const AssetRecord& record, const TextureImportSettings& import_settings);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> create_runtime_material(
            const AssetRecord& record);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> create_runtime_material(
            const AssetRecord& record, const MaterialData& data);

        ProjectPaths m_paths;
        AssetDatabase m_database;
        std::unique_ptr<ImportService> m_import_service;
        AssetRegistry& m_registry;
        RenderResourceFactory& m_resource_factory;
        std::unique_ptr<AssetTaskQueue> m_task_queue;
    };
}
