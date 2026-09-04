#include "asset/import/import_service.h"

#include "asset/import/mesh_importer.h"

#include <utility>

namespace Comet {
    ImportService::ImportService(ProjectPaths paths) : m_paths(std::move(paths)) {}

    std::filesystem::path ImportService::mesh_artifact_path(
        const AssetHandle handle) const {
        return m_paths.cache() / "imported" / "mesh"
               / (std::to_string(handle.value()) + ".bin");
    }

    std::optional<MeshArtifact> ImportService::find_current_mesh_artifact(
        const AssetHandle handle, const std::filesystem::path& source_path) const {
        auto artifact = MeshArtifact::load(mesh_artifact_path(handle), handle);
        if(!artifact || artifact->handle != handle
            || artifact->importer_version != MeshImporter::VERSION
            || artifact->source_inputs.files.front().relative_path
                   != source_path.lexically_normal()
            || !import_inputs_are_current(m_paths.assets(), artifact->source_inputs)) {
            return std::nullopt;
        }
        return artifact;
    }

    MeshArtifact ImportService::build_mesh_artifact(
        const AssetHandle handle, const std::filesystem::path& source_path) const {
        const std::filesystem::path absolute_source = m_paths.assets() / source_path;
        MeshImportResult imported =
            MeshImporter{}.import_with_dependencies(absolute_source);
        return {.handle = handle,
            .importer_version = MeshImporter::VERSION,
            .source_inputs = capture_import_inputs(
                m_paths.assets(), absolute_source, imported.source_dependencies),
            .data = std::move(imported.data)};
    }

}
