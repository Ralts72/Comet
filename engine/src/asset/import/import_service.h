#pragma once

#include "asset/artifact/mesh_artifact.h"
#include "asset/handle.h"
#include "common/export.h"
#include "core/project_paths.h"

#include <filesystem>
#include <optional>

namespace Comet {
    class COMET_API ImportService final {
    public:
        explicit ImportService(ProjectPaths paths);

        [[nodiscard]] std::filesystem::path mesh_artifact_path(AssetHandle handle) const;
        [[nodiscard]] std::optional<MeshArtifact> find_current_mesh_artifact(
            AssetHandle handle, const std::filesystem::path& source_path) const;
        [[nodiscard]] MeshArtifact build_mesh_artifact(
            AssetHandle handle, const std::filesystem::path& source_path) const;

    private:
        ProjectPaths m_paths;
    };
}
