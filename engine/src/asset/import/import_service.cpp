#include "asset/import/import_service.h"

#include "asset/import/mesh_importer.h"
#include "asset/default_assets.h"
#include "asset/import/environment_importer.h"
#include "asset/import/texture_importer.h"
#include "asset/serialization/metadata_serializer.h"

#include <utility>

namespace Comet {
    ImportService::ImportService(ProjectPaths paths, AssetImportLimits limits)
        : m_paths(std::move(paths)), m_limits(limits) {}

    std::filesystem::path ImportService::model_artifact_path(AssetHandle handle) const {
        return m_paths.cache() / "imported" / "model" / std::to_string(handle.value())
               / "index.bin";
    }

    std::optional<ModelArtifact> ImportService::find_current_model_artifact(
        const AssetRecord& record) const {
        if(record.type != AssetType::Model || record.source_asset)
            return std::nullopt;
        auto artifact = ModelArtifact::load(model_artifact_path(record.handle), record.handle);
        if(!artifact || artifact->importer_version != ModelArtifact::IMPORTER_VERSION
            || artifact->source_inputs.files.front().relative_path != record.path.lexically_normal()
            || !import_inputs_are_current(m_paths.assets(), artifact->source_inputs))
            return std::nullopt;
        const auto metadata =
            MetadataSerializer{}.load(metadata_path(m_paths.assets() / record.path));
        if(!metadata || metadata.value() != artifact->metadata)
            return std::nullopt;
        return artifact;
    }

    Result<MeshData> ImportService::prepare_default_character() {
        static const auto imported = MeshImporter{}.import(default_character_source());
        return imported;
    }

    Result<ModelImportPrepared> ImportService::stage_model(
        const AssetRecord& record, std::size_t memory_budget, AssetImportMode mode) const {
        using Prepared = Result<ModelImportPrepared>;
        if(record.type != AssetType::Model || record.source_asset)
            return Prepared::failure("Expected a source model asset");
        if(mode == AssetImportMode::IfNeeded)
            if(auto cached = find_current_model_artifact(record)) {
                const auto metadata = cached->metadata;
                return Prepared::success({std::move(*cached), metadata, true});
            }
        const auto absolute_source = m_paths.resolve_asset_path(record.path);
        if(!absolute_source)
            return Prepared::failure(absolute_source.error());
        const auto sidecar = metadata_path(absolute_source.value());
        const MetadataSerializer serializer;
        const auto metadata = serializer.load(sidecar);
        if(!metadata)
            return Prepared::failure(metadata.error());
        if(metadata.value().handle != record.handle || metadata.value().type != AssetType::Model)
            return Prepared::failure("Model metadata does not match its indexed identity");
        const auto index_path = model_artifact_path(record.handle);
        const auto inspected = ModelImporter::inspect_source(absolute_source.value(), m_limits);
        if(!inspected)
            return Prepared::failure(inspected.error());
        if(inspected.value().working_bytes > memory_budget)
            return Prepared::failure("Model exceeds its reserved working budget");
        const auto before = capture_import_inputs(
            m_paths.assets(), absolute_source.value(), inspected.value().dependencies);
        if(!before)
            return Prepared::failure(before.error());
        auto imported = ModelImporter{}.import(absolute_source.value(), memory_budget, m_limits);
        if(!imported)
            return Prepared::failure(imported.error());
        const auto after = capture_import_inputs(
            m_paths.assets(), absolute_source.value(), imported.value().source_dependencies);
        if(!after)
            return Prepared::failure(after.error());
        if(after.value() != before.value())
            return Prepared::failure("Model inputs changed during import");
        auto product = ModelImporter::bind(std::move(imported).value(), metadata.value());
        if(!product)
            return Prepared::failure(product.error());
        auto stage_limits = m_limits;
        stage_limits.mesh_working_bytes = std::min(stage_limits.mesh_working_bytes, memory_budget);
        auto staged = ModelArtifact::stage(
            index_path, std::move(product).value(), before.value(), stage_limits);
        if(!staged)
            return Prepared::failure(staged.error());
        return Prepared::success({std::move(staged).value(), metadata.value()});
    }

    Result<void> ImportService::publish_model(
        const AssetRecord& record, const ModelImportPrepared& prepared) const {
        const auto& artifact = prepared.artifact;
        if(record.type != AssetType::Model || record.source_asset
            || artifact.metadata.handle != record.handle
            || prepared.source_metadata.handle != record.handle
            || artifact.source_inputs.files.empty()
            || artifact.source_inputs.files.front().relative_path != record.path.lexically_normal())
            return Result<void>::failure("Model candidate identity does not match its source");
        const auto sidecar = metadata_path(m_paths.assets() / record.path);
        const MetadataSerializer serializer;
        const auto metadata = serializer.load(sidecar);
        if(!metadata || metadata.value() != prepared.source_metadata
            || !import_inputs_are_current(m_paths.assets(), artifact.source_inputs))
            return Result<void>::failure("Model inputs changed before publication");
        if(prepared.from_cache)
            return Result<void>::success();
        if(artifact.metadata != prepared.source_metadata)
            if(auto saved = serializer.save(artifact.metadata, sidecar); !saved)
                return saved;
        return artifact.publish_atomic(model_artifact_path(record.handle));
    }

    Result<ModelArtifact> ImportService::prepare_model(
        const AssetRecord& record, std::size_t memory_budget) const {
        auto prepared = stage_model(record, memory_budget);
        if(!prepared)
            return Result<ModelArtifact>::failure(prepared.error());
        if(auto published = publish_model(record, prepared.value()); !published)
            return Result<ModelArtifact>::failure(published.error());
        return Result<ModelArtifact>::success(std::move(prepared).value().artifact);
    }

    MeshArtifactCandidate ImportService::prepare_mesh(const AssetRecord& record,
        const AssetRevision revision, const AssetImportMode mode,
        const std::size_t memory_budget) const {
        if(mode == AssetImportMode::IfNeeded) {
            if(auto artifact =
                    find_current_mesh_artifact(record.handle, record.path, memory_budget))
                return {record.handle, revision, record.path,
                    Result<MeshArtifact>::success(std::move(*artifact)), true};
        }
        return {record.handle, revision, record.path,
            build_mesh_artifact(record.handle, record.path, memory_budget)};
    }

    std::optional<TextureArtifact> ImportService::find_current_texture_artifact(
        const AssetRecord& record, const TextureImportSettings& settings,
        std::size_t memory_budget) const {
        const auto inputs =
            capture_import_inputs(m_paths.assets(), m_paths.assets() / record.path, {});
        if(!inputs || inputs.value().files.front().size > m_limits.source_bytes)
            return std::nullopt;
        auto artifact = TextureArtifact::load(
            texture_artifact_path(record.handle), record.handle, memory_budget);
        if(!artifact || artifact->importer_version != TextureImporter::VERSION
            || artifact->source != inputs.value().files.front() || artifact->settings != settings
            || !import_inputs_are_current(m_paths.assets(), inputs.value()))
            return std::nullopt;
        return artifact;
    }

    Result<TextureData> ImportService::prepare_texture(const AssetRecord& record,
        const TextureImportSettings& settings, const std::size_t memory_budget) const {
        if(auto cached = find_current_texture_artifact(record, settings, memory_budget))
            return Result<TextureData>::success(std::move(cached->data));
        const auto source = m_paths.assets() / record.path;
        auto inputs = capture_import_inputs(m_paths.assets(), source, {});
        if(!inputs)
            return Result<TextureData>::failure(inputs.error());
        const auto& fingerprint = inputs.value().files.front();
        if(fingerprint.size > m_limits.source_bytes)
            return Result<TextureData>::failure("Texture source exceeds its configured limit");
        const auto path = texture_artifact_path(record.handle);
        auto imported = TextureImporter{}.import(source, settings, memory_budget, m_limits);
        if(!imported)
            return imported;
        if(!import_inputs_are_current(m_paths.assets(), inputs.value()))
            return Result<TextureData>::failure("Texture changed during preparation");
        TextureArtifact artifact{.handle = record.handle,
            .importer_version = TextureImporter::VERSION,
            .source = fingerprint,
            .settings = settings,
            .data = std::move(imported).value()};
        // 只发布 CPU 产物；资产 owner 仍须复核 revision 后接纳 GPU 资源。
        if(auto saved = artifact.publish_atomic(path); !saved)
            return Result<TextureData>::failure(saved.error());
        return Result<TextureData>::success(std::move(artifact.data));
    }

    std::filesystem::path ImportService::texture_artifact_path(const AssetHandle handle) const {
        return m_paths.cache() / "imported" / "texture" / (std::to_string(handle.value()) + ".bin");
    }

    std::filesystem::path ImportService::environment_artifact_path(const AssetHandle handle) const {
        return m_paths.cache() / "imported" / "environment"
               / (std::to_string(handle.value()) + ".bin");
    }

    std::optional<EnvironmentArtifact> ImportService::find_current_environment_artifact(
        const AssetRecord& record, std::size_t memory_budget) const {
        const auto inputs =
            capture_import_inputs(m_paths.assets(), m_paths.assets() / record.path, {});
        if(!inputs)
            return std::nullopt;
        auto artifact =
            EnvironmentArtifact::load(environment_artifact_path(record.handle), record.handle,
                memory_budget, inputs.value().files.front(), EnvironmentImporter::VERSION);
        if(!artifact || !import_inputs_are_current(m_paths.assets(), inputs.value()))
            return std::nullopt;
        return artifact;
    }

    Result<EnvironmentArtifact> ImportService::prepare_environment(const AssetRecord& record,
        const std::size_t memory_budget, EnvironmentImporter::Preview* preview) const {
        if(auto cached = find_current_environment_artifact(record, memory_budget))
            return Result<EnvironmentArtifact>::success(std::move(*cached));
        const auto source = m_paths.assets() / record.path;
        auto inputs = capture_import_inputs(m_paths.assets(), source, {});
        if(!inputs)
            return Result<EnvironmentArtifact>::failure(inputs.error());
        const auto& fingerprint = inputs.value().files.front();
        auto imported = EnvironmentImporter{}.import(source, memory_budget, preview);
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

    std::optional<MeshArtifact> ImportService::find_current_mesh_artifact(const AssetHandle handle,
        const std::filesystem::path& source_path, const std::size_t memory_budget) const {
        auto artifact = MeshArtifact::load(mesh_artifact_path(handle), handle, memory_budget);
        if(!artifact || artifact->handle != handle
            || artifact->importer_version != MeshImporter::VERSION
            || artifact->source_inputs.files.front().relative_path != source_path.lexically_normal()
            || !import_inputs_are_current(m_paths.assets(), artifact->source_inputs)) {
            return std::nullopt;
        }
        return artifact;
    }

    Result<MeshArtifact> ImportService::build_mesh_artifact(const AssetHandle handle,
        const std::filesystem::path& source_path, const std::size_t memory_budget) const {
        const auto absolute_source = m_paths.assets() / source_path;
        const auto inspected = MeshImporter::inspect_source(absolute_source, m_limits);
        if(!inspected)
            return Result<MeshArtifact>::failure(inspected.error());
        if(inspected.value().working_bytes > memory_budget)
            return Result<MeshArtifact>::failure("Mesh exceeds its reserved CPU memory budget");
        const auto before = capture_import_inputs(
            m_paths.assets(), absolute_source, inspected.value().dependencies);
        if(!before)
            return Result<MeshArtifact>::failure(before.error());
        auto imported =
            MeshImporter{}.import_with_dependencies(absolute_source, memory_budget, m_limits);
        if(!imported)
            return Result<MeshArtifact>::failure(imported.error());
        const auto after = capture_import_inputs(
            m_paths.assets(), absolute_source, imported.value().source_dependencies);
        if(!after)
            return Result<MeshArtifact>::failure(after.error());
        if(after.value() != before.value())
            return Result<MeshArtifact>::failure("Mesh inputs changed during import");
        return Result<MeshArtifact>::success({.handle = handle,
            .importer_version = MeshImporter::VERSION,
            .source_inputs = before.value(),
            .data = std::move(imported).value().data});
    }
}
