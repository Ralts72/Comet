#include "asset/import/import_service.h"

#include "asset/import/mesh_importer.h"
#include "asset/import/environment_importer.h"
#include "asset/import/texture_importer.h"

#include <utility>

namespace Comet {
    ImportService::ImportService(ProjectPaths paths) : m_paths(std::move(paths)) {}

    MeshArtifactCandidate ImportService::prepare_mesh(
        const AssetRecord& record, const AssetRevision revision, const MeshImportMode mode) const {
        if(mode == MeshImportMode::IfNeeded) {
            if(auto artifact = find_current_mesh_artifact(record.handle, record.path))
                return {record.handle, revision, record.path,
                    Result<MeshArtifact>::success(std::move(*artifact)), true};
        }
        return {
            record.handle, revision, record.path, build_mesh_artifact(record.handle, record.path)};
    }

    Result<TextureData> ImportService::prepare_texture(const AssetRecord& record,
        const TextureImportSettings& settings, const std::size_t memory_budget) const {
        return TextureImporter{}.import(m_paths.assets() / record.path, settings, memory_budget);
    }

    std::filesystem::path ImportService::environment_artifact_path(const AssetHandle handle) const {
        return m_paths.cache() / "imported" / "environment"
               / (std::to_string(handle.value()) + ".bin");
    }

    Result<EnvironmentArtifact> ImportService::prepare_environment(
        const AssetRecord& record, const std::size_t memory_budget) const {
        const auto source = m_paths.assets() / record.path;
        auto inputs = capture_import_inputs(m_paths.assets(), source, {});
        if(!inputs)
            return Result<EnvironmentArtifact>::failure(inputs.error());
        const auto& fingerprint = inputs.value().files.front();
        if(auto cached = EnvironmentArtifact::load(
               environment_artifact_path(record.handle), record.handle, memory_budget)) {
            if(cached->importer_version == EnvironmentImporter::VERSION
                && cached->source == fingerprint
                && import_inputs_are_current(m_paths.assets(), inputs.value()))
                return Result<EnvironmentArtifact>::success(std::move(*cached));
        }
        auto imported = EnvironmentImporter{}.import(source, memory_budget);
        if(!imported)
            return Result<EnvironmentArtifact>::failure(imported.error());
        if(!import_inputs_are_current(m_paths.assets(), inputs.value()))
            return Result<EnvironmentArtifact>::failure("Environment changed during preparation");
        EnvironmentArtifact artifact{.handle = record.handle,
            .importer_version = EnvironmentImporter::VERSION,
            .source = fingerprint,
            .data = std::move(imported).value()};
        // 后台线程仅写入 CPU 缓存，不发布运行时或 GPU 对象。
        if(auto saved = artifact.publish_atomic(environment_artifact_path(record.handle)); !saved)
            return Result<EnvironmentArtifact>::failure(saved.error());
        return Result<EnvironmentArtifact>::success(std::move(artifact));
    }

    std::filesystem::path ImportService::mesh_artifact_path(const AssetHandle handle) const {
        return m_paths.cache() / "imported" / "mesh" / (std::to_string(handle.value()) + ".bin");
    }

    std::filesystem::path ImportService::shader_program_artifact_path(
        const AssetHandle handle) const {
        return m_paths.cache() / "shaders" / (std::to_string(handle.value()) + ".csp");
    }

    std::optional<MeshArtifact> ImportService::find_current_mesh_artifact(
        const AssetHandle handle, const std::filesystem::path& source_path) const {
        auto artifact = MeshArtifact::load(mesh_artifact_path(handle), handle);
        if(!artifact || artifact->handle != handle
            || artifact->importer_version != MeshImporter::VERSION
            || artifact->source_inputs.files.front().relative_path != source_path.lexically_normal()
            || !import_inputs_are_current(m_paths.assets(), artifact->source_inputs)) {
            return std::nullopt;
        }
        return artifact;
    }

    Result<MeshArtifact> ImportService::build_mesh_artifact(
        const AssetHandle handle, const std::filesystem::path& source_path) const {
        const auto absolute_source = m_paths.assets() / source_path;
        auto imported = MeshImporter{}.import_with_dependencies(absolute_source);
        if(!imported)
            return Result<MeshArtifact>::failure(imported.error());
        auto inputs = capture_import_inputs(
            m_paths.assets(), absolute_source, imported.value().source_dependencies);
        if(!inputs)
            return Result<MeshArtifact>::failure(inputs.error());
        return Result<MeshArtifact>::success({.handle = handle,
            .importer_version = MeshImporter::VERSION,
            .source_inputs = std::move(inputs).value(),
            .data = std::move(imported).value().data});
    }
}
