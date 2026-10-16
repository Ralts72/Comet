#pragma once

#include "asset/content_fwd.h"

#include "animation/animation_controller.h"
#include "asset/database.h"
#include "asset/data/material_data.h"
#include "asset/data/mesh_data.h"
#include "asset/data/texture_data.h"
#include "asset/import/asset_task_types.h"
#include "asset/reference.h"
#include "common/export.h"
#include "common/error.h"
#include "common/result.h"
#include "core/project_paths.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Comet {
    class AssetRegistry;
    class Scene;
    class ComponentRegistry;
    struct ModelData;
    class ImportService;
    class AssetLoader;
    class RenderAssetFactory;
    class RenderResourceFactory;
    class TaskScheduler;
    class Script;
    class AudioClip;
    struct MaterialData;
    struct MeshData;
    class AssetTaskQueue;
    struct AssetImportResult;
    struct MeshArtifactCandidate;
    struct ModelImportCandidate;
    struct MaterialImportCandidate;
    struct TextureImportCandidate;
    struct EnvironmentImportCandidate;
    struct ShaderProgramImportRequest;
    struct ShaderProgramImportPrepared;
    struct ShaderProgramImportFailure;
    struct ShaderProgramImportCandidate;
    class ShaderProgramArtifact;
    struct Environment;

    class COMET_API AssetManager final {
    public:
        class COMET_API MaterialUpdate {
        public:
            [[nodiscard]] AssetHandle handle() const { return m_record.handle; }
            [[nodiscard]] std::shared_ptr<const Material> material() const;
            [[nodiscard]] std::shared_ptr<Material> expected_material() const { return m_expected; }

        private:
            friend class AssetManager;
            MaterialUpdate() = default;
            const AssetManager* m_owner = nullptr;
            AssetRecord m_record;
            AssetRevision m_revision = 0;
            uint64_t m_publication = 0;
            MaterialData m_data;
            std::shared_ptr<Material> m_material;
            std::shared_ptr<Material> m_expected;
        };

        AssetManager(ProjectPaths paths, AssetRegistry& registry,
            RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler);
        AssetManager(ProjectPaths paths, AssetRegistry& registry,
            RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler,
            AssetImportLimits limits);
        // 借用项目索引；database 必须比 AssetManager 活得更久。
        AssetManager(AssetDatabase& database, AssetRegistry& registry,
            RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler,
            AssetImportLimits limits = {});
        ~AssetManager();

        [[nodiscard]] AssetScanReport scan();
        // 外部索引 owner 修改数据库后调用一次，再处理后台完成结果。
        void accept_scan_report(const AssetScanReport& report);
        // 本次成功发布的结果；Mesh Artifact 发布不代表 GPU 已驻留。
        [[nodiscard]] Result<std::vector<AssetHandle>, Error> process_completions();
        // 失败／过期也计数；昂贵重试按资产或 Script 原子组占用余量。
        // 时间预算不抢占单个发布／重试操作。
        [[nodiscard]] Result<std::vector<AssetHandle>, Error> process_completions(
            AssetCompletionBudget budget);
        [[nodiscard]] AssetAsyncStatus get_async_status() const;
        struct Residency {
            std::size_t registry_assets = 0;
            std::size_t model_cache_entries = 0;
        };
        // 当前 Registry 与 CPU 模型缓存的条目数；GPU 内存见渲染诊断。
        [[nodiscard]] Residency residency() const;
        struct ResidencyRelease {
            std::size_t registry_assets = 0;
            std::size_t model_cache_entries = 0;
        };
        // 宿主仅在成功激活后提交全部需求，包括 Edit 基线与预览；失败候选不得调用。
        // 数据库依赖与模型源形成闭包；内置身份、未索引的外部资源始终保留。
        [[nodiscard]] ResidencyRelease release_unused(std::span<const AssetReference> references,
            std::span<const AssetHandle> retained_roots = {});
        [[nodiscard]] Result<void, Error> ensure_loaded(
            AssetHandle handle, AssetType expected_type);
        // 环境资源异步准备；成功仅表示已接受加载需求，不保证 GPU 资源已驻留。
        [[nodiscard]] Result<void, Error> request_load(AssetHandle handle, AssetType expected_type);
        [[nodiscard]] Result<void, Error> request_load(const AssetReference& reference);
        enum class MissingAssetPolicy { FailRequired, AllowMissing };
        // 入关时准备源资产并提交运行资源加载；是否已就绪由 references_ready 查询。
        [[nodiscard]] Result<std::vector<AssetReference>, Error> prepare_scene(
            Scene& scene, const ComponentRegistry& components);
        [[nodiscard]] Result<std::size_t, Error> prepare_references(
            std::span<const AssetReference> references, MissingAssetPolicy policy);
        [[nodiscard]] Result<std::vector<AssetReference>, Error> collect_scene_references(
            Scene& scene, const ComponentRegistry& components,
            MissingAssetPolicy policy = MissingAssetPolicy::FailRequired);
        [[nodiscard]] Result<std::shared_ptr<GameData>, Error> load_data(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<NavigationData>, Error> load_navigation(
            AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Irradiance>, Error> load_irradiance(
            AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<AnimationLibrary>, Error> load_animation_library(
            AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<AnimationController>, Error> load_animation_controller(
            AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Prefab>, Error> load_prefab(
            AssetHandle handle, const ComponentRegistry& components);
        // prepare_references 后只读查询；临时环境预览不算完整资源就绪。
        [[nodiscard]] Result<bool, Error> references_ready(
            std::span<const AssetReference> references, MissingAssetPolicy policy) const;
        [[nodiscard]] Result<void, Error> import_model(AssetHandle handle);
        [[nodiscard]] bool import_model_async(
            AssetHandle handle, AssetImportMode mode = AssetImportMode::IfNeeded);
        [[nodiscard]] Result<std::shared_ptr<const ModelData>, Error> load_model(
            AssetHandle handle);
        [[nodiscard]] Result<void, Error> import_mesh(AssetHandle handle);
        [[nodiscard]] bool import_mesh_async(
            AssetHandle handle, AssetImportMode mode = AssetImportMode::IfNeeded);
        [[nodiscard]] bool is_import_pending(AssetHandle handle) const;
        // 包含在途和失败的导入；成功确认 Artifact 后移除。
        [[nodiscard]] std::vector<AssetHandle> source_imports_needing_recheck() const;
        using ShaderProgramPrepare =
            std::function<Result<ShaderProgramImportPrepared, ShaderProgramImportFailure>()>;
        [[nodiscard]] bool import_shader_program_async(
            ShaderProgramImportRequest request, ShaderProgramPrepare prepare);
        [[nodiscard]] std::shared_ptr<const ShaderProgramArtifact> compiled_shader_program(
            AssetHandle handle) const;
        // 编译失败后停止解析旧产物；已提交的帧仍持有自己的引用。
        void invalidate_shader_program(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<ShaderProgramArtifact>, Error> load_shader_program(
            AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Mesh>, Error> load_mesh(AssetHandle handle);
        // 读取已导入的 CPU 几何；不创建 GPU 资源，也不发布到运行时 Registry。
        [[nodiscard]] Result<MeshData, Error> read_mesh_data(AssetHandle handle);
        [[nodiscard]] uint64_t mesh_data_revision(AssetHandle handle) const;
        // 只读取已有 CPU 导入产物；不会触发同步导入或创建 GPU 资源。
        [[nodiscard]] Result<TextureData, Error> read_texture_data(AssetHandle handle);
        [[nodiscard]] Result<TextureData, Error> read_environment_background(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Texture>, Error> load_texture(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Script>, Error> load_script(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<AudioClip>, Error> load_audio(AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<AudioClip>, Error> reimport_audio(
            AssetHandle handle, AudioImportSettings settings);
        [[nodiscard]] Result<std::shared_ptr<Environment>, Error> load_environment(
            AssetHandle handle);
        [[nodiscard]] Result<std::shared_ptr<Texture>, Error> reimport_texture(
            AssetHandle handle, TextureImportSettings import_settings);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> load_material(AssetHandle handle);
        [[nodiscard]] Result<MaterialUpdate, Error> prepare_material_update(
            AssetHandle handle, const MaterialData& data);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> commit_material_update(
            const MaterialUpdate& update);
        // 预览只替换运行版本；文件与依赖仍在最终提交时保存。
        [[nodiscard]] Result<void, Error> preview_material_update(MaterialUpdate& update);
        [[nodiscard]] Result<void, Error> restore_material_preview(
            const MaterialUpdate& update, const std::shared_ptr<Material>& previous);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> reload_material(AssetHandle handle);

        [[nodiscard]] const AssetDatabase& get_database() const noexcept { return m_database; }

    private:
        [[nodiscard]] Result<void> update_import_dependencies(
            AssetHandle handle, std::vector<std::filesystem::path> dependencies);
        enum class RefreshResult { Scheduled, Deferred, Invalidated, Rejected };
        [[nodiscard]] RefreshResult schedule_refresh(const AssetRecord& record);
        struct ScriptReloadSource;
        using ScriptReloadSources = std::map<AssetHandle, ScriptReloadSource>;
        ScriptReloadSource script_reload_source(const AssetRecord& record) const;
        ScriptReloadSources script_reload_sources() const;
        Result<void, Error> publish_script_group(AssetHandle seed,
            std::unordered_set<AssetHandle>& processed, ScriptReloadSources& sources);
        std::vector<AssetHandle> refresh_scripts(std::span<const AssetHandle> handles);
        std::vector<AssetHandle> refresh_scripts(std::span<const AssetHandle> handles,
            AssetCompletionBudget& budget, std::chrono::steady_clock::time_point start);
        void retry_refresh_requests(
            std::vector<AssetHandle>& published, AssetCompletionBudget& budget);
        Result<void, Error> reload_loaded_material_dependents(AssetHandle texture_handle);
        Result<void, Error> publish_material(AssetHandle handle, const MaterialData& data,
            const std::shared_ptr<Material>& material, bool replace_existing);
        [[nodiscard]] bool material_update_is_current(const MaterialUpdate& update) const;
        Result<void, Error> refresh_loaded_mesh(
            AssetHandle handle, AssetRevision revision, MeshData data);
        bool record_import_dependencies(
            AssetHandle handle, const std::vector<std::filesystem::path>& dependencies);
        void complete_mesh_import(AssetHandle handle, AssetRevision revision,
            const std::vector<std::filesystem::path>& dependencies);
        [[nodiscard]] bool schedule_mesh_task(const AssetRecord& record, AssetImportMode mode);
        [[nodiscard]] bool schedule_loaded_texture_refresh(const AssetRecord& record);
        [[nodiscard]] bool schedule_material_refresh(const AssetRecord& record);
        [[nodiscard]] Result<bool, Error> schedule_environment(const AssetRecord& record);
        enum class EnvironmentState { Unloaded, Preparing, Ready, Failed };
        [[nodiscard]] Result<EnvironmentState, Error> environment_state(AssetHandle handle) const;
        Result<void, Error> publish_environment_previews(AssetCompletionBudget budget);
        // 空值表示未发布；Handle 表示已发布；错误表示不能继续处理队列。
        using ImportPublication = Result<std::optional<AssetHandle>, Error>;
        ImportPublication publish_import_result(AssetImportResult& result);
        void invalidate_model(AssetHandle handle);
        ImportPublication publish_model_candidate(ModelImportCandidate& candidate);
        ImportPublication publish_mesh_candidate(MeshArtifactCandidate& candidate);
        ImportPublication publish_material_candidate(MaterialImportCandidate& candidate);
        ImportPublication publish_texture_candidate(TextureImportCandidate& candidate);
        ImportPublication publish_environment_candidate(EnvironmentImportCandidate& candidate);
        ImportPublication publish_shader_program_candidate(ShaderProgramImportCandidate& candidate);

        AssetImportLimits m_limits;
        struct MeshDataState {
            uint64_t revision = 0;
            bool available = true;
            bool prepared = false;
        };
        void change_mesh_data(AssetHandle handle, bool available);
        void publish_model_meshes(AssetHandle model);
        uint64_t m_mesh_data_revision = 0;
        std::unordered_map<AssetHandle, MeshDataState> m_mesh_data;
        std::unique_ptr<AssetDatabase> m_owned_database;
        AssetDatabase& m_database;
        std::unique_ptr<ImportService> m_import_service;
        AssetRegistry& m_registry;
        std::unique_ptr<RenderAssetFactory> m_render_factory;
        std::unique_ptr<AssetLoader> m_loader;
        std::unique_ptr<AssetTaskQueue> m_task_queue;
        std::unordered_map<AssetHandle, AssetRevision> m_refresh_requests;
        std::unordered_map<AssetHandle, AssetRevision> m_failed_environments;
        struct PendingEnvironmentPreview;
        std::unordered_map<AssetHandle, std::shared_ptr<PendingEnvironmentPreview>>
            m_environment_previews;
        std::unordered_set<AssetHandle> m_source_imports_needing_recheck;
        // 显式释放后拒绝旧后台任务重新发布；下一次真实加载请求解除标记。
        std::unordered_set<AssetHandle> m_released_assets;
        bool m_processing_completions = false;
    };
}
