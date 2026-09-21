#pragma once

#include "asset/database.h"
#include "asset/data/material_data.h"
#include "asset/import/asset_task_types.h"
#include "asset/reference.h"
#include "common/export.h"
#include "common/error.h"
#include "common/result.h"
#include "core/project_paths.h"

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

namespace Comet {
    class AssetRegistry;
    class ImportService;
    class Material;
    class Mesh;
    class RenderResourceFactory;
    class TaskScheduler;
    class Texture;
    class Script;
    struct MaterialData;
    struct MeshData;
    struct GraphicsError;
    class AssetTaskQueue;
    struct AssetImportResult;
    struct MeshArtifactCandidate;
    struct MaterialImportCandidate;
    struct TextureImportCandidate;
    struct EnvironmentImportCandidate;
    struct Environment;

    class COMET_API AssetManager final {
    public:
        class COMET_API MaterialUpdate {
        public:
            [[nodiscard]] AssetHandle handle() const { return m_record.handle; }
            [[nodiscard]] std::shared_ptr<const Material> material() const;

        private:
            friend class AssetManager;
            MaterialUpdate() = default;
            const AssetManager* m_owner = nullptr;
            AssetRecord m_record;
            AssetRevision m_revision = 0;
            MaterialData m_data;
            std::string m_serialized;
            std::shared_ptr<Material> m_material;
            std::shared_ptr<Material> m_previous;
        };

        AssetManager(ProjectPaths paths, AssetRegistry& registry,
            RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler);
        AssetManager(ProjectPaths paths, AssetRegistry& registry,
            RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler,
            AssetAsyncLimits limits);
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
            AssetCompletionBudget budget);
        [[nodiscard]] AssetAsyncStatus get_async_status() const;
        [[nodiscard]] Result<void, Error> ensure_loaded(
            AssetHandle handle, AssetType expected_type);
        // 环境资源异步准备；成功仅表示已接受加载需求，不保证 GPU 资源已驻留。
        [[nodiscard]] Result<void, Error> request_load(AssetHandle handle, AssetType expected_type);
        enum class MissingAssetPolicy { FailRequired, AllowMissing };
        [[nodiscard]] Result<std::size_t, Error> prepare_references(
            std::span<const AssetReference> references, MissingAssetPolicy policy);
        [[nodiscard]] Result<void, Error> import_mesh(AssetHandle handle);
        [[nodiscard]] bool import_mesh_async(
            AssetHandle handle, MeshImportMode mode = MeshImportMode::IfNeeded);
        [[nodiscard]] Result<std::shared_ptr<Mesh>, Error> load_mesh(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Texture>, Error> load_texture(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Script>, Error> load_script(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Environment>, Error> load_environment(
            AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Texture>, Error> reimport_texture(
            AssetHandle handle, TextureImportSettings import_settings);
        [[nodiscard]] AssetScanReport create_material(
            const std::filesystem::path& destination, const MaterialData& data);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> load_material(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> update_material(
            AssetHandle handle, const MaterialData& data);
        [[nodiscard]] Result<MaterialUpdate, Error> prepare_material_update(
            AssetHandle handle, const MaterialData& data);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> commit_material_update(
            const MaterialUpdate& update);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> reload_material(AssetHandle handle);

        [[nodiscard]] const AssetDatabase& get_database() const noexcept { return m_database; }

    private:
        void apply_scan_report(const AssetScanReport& report);
        enum class RefreshResult { Scheduled, Deferred, Invalidated, Rejected };
        [[nodiscard]] RefreshResult schedule_refresh(const AssetRecord& record);
        void retry_refresh_requests();
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
        [[nodiscard]] Result<bool, Error> schedule_environment(const AssetRecord& record);
        // 空值表示未发布；Handle 表示已发布；错误表示不能继续处理队列。
        using ImportPublication = Result<std::optional<AssetHandle>, Error>;
        ImportPublication publish_import_result(AssetImportResult& result);
        ImportPublication publish_mesh_candidate(MeshArtifactCandidate& candidate);
        ImportPublication publish_material_candidate(MaterialImportCandidate& candidate);
        ImportPublication publish_texture_candidate(TextureImportCandidate& candidate);
        ImportPublication publish_environment_candidate(EnvironmentImportCandidate& candidate);
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
        std::unordered_map<AssetHandle, AssetRevision> m_refresh_requests;
        std::unordered_map<AssetHandle, AssetRevision> m_failed_environments;
    };
}
