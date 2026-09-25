#pragma once

#include "asset/artifact/mesh_artifact.h"
#include "asset/artifact/environment_artifact.h"
#include "asset/import/import_candidate.h"
#include "asset/import/asset_task_types.h"
#include "asset/handle.h"
#include "common/export.h"
#include "common/result.h"
#include "core/project_paths.h"

#include <filesystem>
#include <optional>

namespace Comet {
    class COMET_API ImportService final {
    public:
        explicit ImportService(ProjectPaths paths);

        [[nodiscard]] std::filesystem::path mesh_artifact_path(AssetHandle handle) const;
        [[nodiscard]] std::filesystem::path shader_program_artifact_path(AssetHandle handle) const;
        [[nodiscard]] std::optional<MeshArtifact> find_current_mesh_artifact(
            AssetHandle handle, const std::filesystem::path& source_path) const;
        [[nodiscard]] Result<MeshArtifact> build_mesh_artifact(
            AssetHandle handle, const std::filesystem::path& source_path) const;
        [[nodiscard]] MeshArtifactCandidate prepare_mesh(
            const AssetRecord& record, AssetRevision revision, MeshImportMode mode) const;
        [[nodiscard]] Result<TextureData> prepare_texture(const AssetRecord& record,
            const TextureImportSettings& settings, std::size_t memory_budget) const;
        [[nodiscard]] std::filesystem::path environment_artifact_path(AssetHandle handle) const;
        [[nodiscard]] Result<EnvironmentArtifact> prepare_environment(
            const AssetRecord& record, std::size_t memory_budget) const;

    private:
        ProjectPaths m_paths;
    };
}
