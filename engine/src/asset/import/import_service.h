#pragma once

#include "asset/artifact/mesh_artifact.h"
#include "asset/artifact/environment_artifact.h"
#include "asset/artifact/model_artifact.h"
#include "asset/artifact/texture_artifact.h"
#include "asset/import/import_candidate.h"
#include "asset/import/asset_task_types.h"
#include "asset/import/environment_importer.h"
#include "asset/handle.h"
#include "common/export.h"
#include "common/result.h"
#include "core/project_paths.h"

#include <filesystem>
#include <optional>

namespace Comet {
    class COMET_API ImportService final {
    public:
        explicit ImportService(ProjectPaths paths, AssetImportLimits limits = {});

        [[nodiscard]] std::filesystem::path model_artifact_path(AssetHandle handle) const;
        [[nodiscard]] std::optional<ModelArtifact> find_current_model_artifact(
            const AssetRecord& record) const;
        [[nodiscard]] static Result<MeshData> prepare_default_character();
        // 后台只写未发布的产品；.meta 和可见索引由 owner 发布。
        [[nodiscard]] Result<ModelImportPrepared> stage_model(const AssetRecord& record,
            std::size_t memory_budget, AssetImportMode mode = AssetImportMode::IfNeeded) const;
        [[nodiscard]] Result<void> publish_model(
            const AssetRecord& record, const ModelImportPrepared& prepared) const;
        // 同步内容导入：成功后模型 .meta 可能新增子资源，索引 owner 需接纳一次扫描。
        [[nodiscard]] Result<ModelArtifact> prepare_model(
            const AssetRecord& record, std::size_t memory_budget) const;

        [[nodiscard]] std::filesystem::path mesh_artifact_path(AssetHandle handle) const;
        [[nodiscard]] std::filesystem::path shader_program_artifact_path(AssetHandle handle) const;
        [[nodiscard]] std::optional<MeshArtifact> find_current_mesh_artifact(AssetHandle handle,
            const std::filesystem::path& source_path, std::size_t memory_budget) const;
        [[nodiscard]] Result<MeshArtifact> build_mesh_artifact(AssetHandle handle,
            const std::filesystem::path& source_path, std::size_t memory_budget) const;
        [[nodiscard]] MeshArtifactCandidate prepare_mesh(const AssetRecord& record,
            AssetRevision revision, AssetImportMode mode, std::size_t memory_budget) const;
        [[nodiscard]] std::optional<TextureArtifact> find_current_texture_artifact(
            const AssetRecord& record, const TextureImportSettings& settings,
            std::size_t memory_budget) const;
        [[nodiscard]] Result<TextureData> prepare_texture(const AssetRecord& record,
            const TextureImportSettings& settings, std::size_t memory_budget) const;
        [[nodiscard]] std::filesystem::path texture_artifact_path(AssetHandle handle) const;
        [[nodiscard]] std::filesystem::path environment_artifact_path(AssetHandle handle) const;
        [[nodiscard]] std::optional<EnvironmentArtifact> find_current_environment_artifact(
            const AssetRecord& record, std::size_t memory_budget) const;
        [[nodiscard]] Result<EnvironmentArtifact> prepare_environment(const AssetRecord& record,
            std::size_t memory_budget, EnvironmentImporter::Preview* preview = nullptr) const;

    private:
        ProjectPaths m_paths;
        AssetImportLimits m_limits;
    };
}
