#include "asset/asset_manager.h"
#include "asset/runtime/asset_loader.h"
#include "asset/runtime/render_asset_publisher.h"
#include "graphics/error.h"
#include "asset/import/asset_task_queue.h"
#include "asset/import/import_candidate.h"
#include "common/result.h"
#include "asset/data/texture_data.h"

#include "asset/artifact/mesh_artifact.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/import/import_service.h"
#include "asset/import/mesh_importer.h"
#include "asset/import/texture_importer.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "common/file_io.h"
#include "diagnostics/logger.h"

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Comet {
    namespace {
        bool validate_asset_handle(const AssetHandle handle, const std::string_view operation) {
            if(handle) {
                return true;
            }
            LOG_ERROR("Cannot {} with an invalid asset handle", operation);
            return false;
        }

        const AssetRecord* find_asset_record(const AssetDatabase& database,
            const AssetHandle handle, const AssetType expected_type) {
            const AssetRecord* record = database.find(handle);
            if(!record) {
                LOG_ERROR("Asset handle {} is not indexed", handle.value());
                return nullptr;
            }
            if(record->type != expected_type) {
                LOG_ERROR("Asset handle {} has type '{}', expected '{}'", handle.value(),
                    to_string(record->type), to_string(expected_type));
                return nullptr;
            }
            return record;
        }

        template<typename T> struct RuntimeAssetLookup {
            std::shared_ptr<T> asset;
            bool type_conflict = false;
        };

        template<typename T>
        RuntimeAssetLookup<T> find_runtime_asset(
            const AssetRegistry& registry, const AssetHandle handle, std::shared_ptr<T> asset) {
            if(asset) {
                return {.asset = std::move(asset)};
            }
            if(registry.contains(handle)) {
                LOG_ERROR("Asset handle {} is already registered with another runtime type",
                    handle.value());
                return {.type_conflict = true};
            }
            return {};
        }

    }

    AssetManager::AssetManager(ProjectPaths paths, AssetRegistry& registry,
        RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler)
        : AssetManager(
              std::move(paths), registry, resource_factory, task_scheduler, AssetImportLimits{}) {}

    AssetManager::AssetManager(ProjectPaths paths, AssetRegistry& registry,
        RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler,
        const AssetImportLimits limits)
        : m_limits(limits), m_owned_database(std::make_unique<AssetDatabase>(std::move(paths))),
          m_database(*m_owned_database),
          m_import_service(std::make_unique<ImportService>(m_database.paths(), m_limits)),
          m_registry(registry),
          m_render_assets(std::make_unique<RenderAssetPublisher>(registry, resource_factory)),
          m_loader(std::make_unique<AssetLoader>(m_database, registry, *m_import_service,
              *m_render_assets, limits.mesh_working_bytes, limits.texture_working_bytes,
              limits.async.working_bytes)),
          m_task_queue(std::make_unique<AssetTaskQueue>(m_database, task_scheduler, limits.async)) {
    }

    AssetManager::AssetManager(AssetDatabase& database, AssetRegistry& registry,
        RenderResourceFactory& resource_factory, TaskScheduler& task_scheduler,
        const AssetImportLimits limits)
        : m_limits(limits), m_database(database),
          m_import_service(std::make_unique<ImportService>(m_database.paths(), m_limits)),
          m_registry(registry),
          m_render_assets(std::make_unique<RenderAssetPublisher>(registry, resource_factory)),
          m_loader(std::make_unique<AssetLoader>(m_database, registry, *m_import_service,
              *m_render_assets, limits.mesh_working_bytes, limits.texture_working_bytes,
              limits.async.working_bytes)),
          m_task_queue(std::make_unique<AssetTaskQueue>(m_database, task_scheduler, limits.async)) {
    }

    AssetManager::~AssetManager() = default;

    Result<void> AssetManager::update_import_dependencies(
        const AssetHandle handle, std::vector<std::filesystem::path> dependencies) {
        return m_database.update_import_dependencies(handle, std::move(dependencies));
    }

    std::shared_ptr<const ShaderProgramArtifact> AssetManager::compiled_shader_program(
        const AssetHandle handle) const {
        return m_registry.resolve<ShaderProgramArtifact>(handle);
    }

    AssetScanReport AssetManager::scan() {
        AssetScanReport report = m_database.scan();
        accept_scan_report(report);
        return report;
    }

    Result<void, Error> AssetManager::ensure_loaded(
        const AssetHandle handle, const AssetType expected_type) {
        switch(expected_type) {
            case AssetType::Audio: {
                auto loaded = load_audio(handle);
                if(!loaded)
                    return Result<void, Error>::failure(loaded.error());
                break;
            }
            case AssetType::Script: {
                auto loaded = load_script(handle);
                if(!loaded)
                    return Result<void, Error>::failure(loaded.error());
                break;
            }
            case AssetType::Environment: {
                auto loaded = load_environment(handle);
                if(!loaded)
                    return Result<void, Error>::failure(loaded.error());
                break;
            }
            case AssetType::Mesh: {
                auto loaded = load_mesh(handle);
                if(!loaded)
                    return Result<void, Error>::failure(loaded.error());
                break;
            }
            case AssetType::Material: {
                auto loaded = load_material(handle);
                if(!loaded)
                    return Result<void, Error>::failure(loaded.error());
                break;
            }
            case AssetType::Texture: {
                auto loaded = load_texture(handle);
                if(!loaded)
                    return Result<void, Error>::failure(loaded.error());
                break;
            }
            case AssetType::ShaderProgram: {
                auto loaded = load_shader_program(handle);
                if(!loaded)
                    return Result<void, Error>::failure(loaded.error());
                break;
            }
            default:
                return Result<void, Error>::failure(
                    {"Runtime loading is not supported for this asset type"});
        }
        return Result<void, Error>::success();
    }

    Result<AssetManager::EnvironmentState, Error> AssetManager::environment_state(
        const AssetHandle handle) const {
        const auto* record = m_database.find(handle);
        if(!record || record->type != AssetType::Environment)
            return Result<EnvironmentState, Error>::failure(
                {"Environment is not indexed: " + std::to_string(handle.value())});
        const auto environment = m_render_assets->environment(handle);
        // 重载失败不影响已发布的完整版本；低清预览不算就绪。
        if(m_render_assets->has_lighting(environment))
            return Result<EnvironmentState, Error>::success(EnvironmentState::Ready);
        if(!environment && m_registry.contains(handle))
            return Result<EnvironmentState, Error>::failure({"Runtime environment type conflict"});
        const auto revision = m_database.get_revision(handle);
        if(const auto failed = m_failed_environments.find(handle);
            failed != m_failed_environments.end() && failed->second == revision)
            return Result<EnvironmentState, Error>::success(EnvironmentState::Failed);
        const auto deferred = m_refresh_requests.find(handle);
        if(m_task_queue->contains(handle, revision)
            || (deferred != m_refresh_requests.end() && deferred->second == revision))
            return Result<EnvironmentState, Error>::success(EnvironmentState::Preparing);
        if(environment)
            return Result<EnvironmentState, Error>::success(EnvironmentState::Failed);
        return Result<EnvironmentState, Error>::success(EnvironmentState::Unloaded);
    }

    Result<void, Error> AssetManager::request_load(
        const AssetHandle handle, const AssetType expected_type) {
        if(expected_type != AssetType::Environment)
            return ensure_loaded(handle, expected_type);
        const auto state = environment_state(handle);
        if(!state)
            return Result<void, Error>::failure(state.error());
        if(state.value() == EnvironmentState::Ready || state.value() == EnvironmentState::Preparing)
            return Result<void, Error>::success();
        if(state.value() == EnvironmentState::Failed)
            return Result<void, Error>::failure(
                {"Environment preparation failed; waiting for source changes"});
        auto scheduled = schedule_environment(*m_database.find(handle));
        if(!scheduled)
            return Result<void, Error>::failure(scheduled.error());
        if(!scheduled.value())
            m_refresh_requests[handle] = m_database.get_revision(handle);
        return Result<void, Error>::success();
    }

    Result<std::size_t, Error> AssetManager::prepare_references(
        const std::span<const AssetReference> references, const MissingAssetPolicy policy) {
        std::size_t missing = 0;
        for(const auto& reference : references) {
            auto loaded = request_load(reference.handle, reference.type);
            if(loaded)
                continue;
            if(is_device_lost(loaded.error())
                || (reference.required && policy == MissingAssetPolicy::FailRequired))
                return Result<std::size_t, Error>::failure(loaded.error());
            LOG_WARN("Unresolved asset {}: {}", reference.handle.value(), loaded.error().message);
            ++missing;
        }
        return Result<std::size_t, Error>::success(missing);
    }

    Result<bool, Error> AssetManager::references_ready(
        const std::span<const AssetReference> references, const MissingAssetPolicy policy) const {
        bool ready = true;
        for(const auto& reference : references) {
            if(reference.type == AssetType::Environment) {
                const auto state = environment_state(reference.handle);
                if(state && state.value() == EnvironmentState::Ready)
                    continue;
                if(state && state.value() == EnvironmentState::Preparing) {
                    ready = false;
                    continue;
                }
            } else {
                const auto* record = m_database.find(reference.handle);
                if(record && record->type == reference.type) {
                    if(m_registry.contains(reference.handle))
                        continue;
                    const auto revision = m_database.get_revision(reference.handle);
                    const auto deferred = m_refresh_requests.find(reference.handle);
                    if(m_task_queue->contains(reference.handle, revision)
                        || (deferred != m_refresh_requests.end() && deferred->second == revision)) {
                        ready = false;
                        continue;
                    }
                }
            }
            if(reference.required && policy == MissingAssetPolicy::FailRequired)
                return Result<bool, Error>::failure(
                    {"Required asset is not ready: " + std::to_string(reference.handle.value())});
        }
        return Result<bool, Error>::success(ready);
    }

    AssetManager::ImportPublication AssetManager::publish_import_result(AssetImportResult& result) {
        if(auto* candidate = std::get_if<MeshArtifactCandidate>(&result.candidate))
            return publish_mesh_candidate(*candidate);
        if(auto* candidate = std::get_if<MaterialImportCandidate>(&result.candidate))
            return publish_material_candidate(*candidate);
        if(auto* candidate = std::get_if<TextureImportCandidate>(&result.candidate))
            return publish_texture_candidate(*candidate);
        if(auto* candidate = std::get_if<EnvironmentImportCandidate>(&result.candidate))
            return publish_environment_candidate(*candidate);
        if(auto* candidate = std::get_if<ShaderProgramImportCandidate>(&result.candidate))
            return publish_shader_program_candidate(*candidate);
        return ImportPublication::failure({"Import task completed without a candidate"});
    }

    AssetManager::ImportPublication AssetManager::publish_shader_program_candidate(
        ShaderProgramImportCandidate& candidate) {
        const auto& request = candidate.request;
        const auto handle = request.handle;
        if(!candidate.result) {
            std::vector<std::filesystem::path> dependencies(
                m_database.get_import_dependencies(handle).begin(),
                m_database.get_import_dependencies(handle).end());
            dependencies.insert(dependencies.end(), candidate.result.error().dependencies.begin(),
                candidate.result.error().dependencies.end());
            if(auto indexed = update_import_dependencies(handle, std::move(dependencies)); !indexed)
                LOG_WARN("Shader program {} dependencies: {}", handle.value(), indexed.error());
            LOG_WARN("Shader program {}: {}", handle.value(), candidate.result.error().message);
            return ImportPublication::success(std::nullopt);
        }

        const auto* program = m_database.find(handle);
        const auto* vertex = m_database.find(request.vertex.handle);
        const auto* fragment = m_database.find(request.fragment.handle);
        if(!program || program->type != AssetType::ShaderProgram || !vertex
            || vertex->type != AssetType::Shader || !fragment || fragment->type != AssetType::Shader
            || !m_database.is_current(request.vertex.handle, request.vertex.revision)
            || !m_database.is_current(request.fragment.handle, request.fragment.revision))
            return ImportPublication::success(std::nullopt);

        auto& prepared = candidate.result.value();
        auto& artifact = prepared.artifact;
        const auto descriptor =
            request.descriptor_path.lexically_relative(m_database.paths().assets());
        if(artifact.handle != handle || artifact.inputs.files.empty()
            || artifact.inputs.files.front().relative_path != descriptor
            || !import_inputs_are_current(m_database.paths().assets(), artifact.inputs)) {
            LOG_WARN("Shader program {} candidate is stale or invalid", handle.value());
            return ImportPublication::success(std::nullopt);
        }
        if(!prepared.from_cache) {
            if(auto saved =
                    artifact.publish_atomic(m_import_service->shader_program_artifact_path(handle));
                !saved) {
                LOG_WARN("Shader program {}: {}", handle.value(), saved.error());
                return ImportPublication::success(std::nullopt);
            }
        }

        std::vector<std::filesystem::path> dependencies;
        for(const auto& file : artifact.inputs.files)
            if(file.relative_path != descriptor)
                dependencies.push_back(file.relative_path);
        if(auto indexed = update_import_dependencies(handle, std::move(dependencies)); !indexed) {
            LOG_WARN("Shader program {} dependencies: {}", handle.value(), indexed.error());
            return ImportPublication::success(std::nullopt);
        }
        auto version = std::make_shared<ShaderProgramArtifact>(std::move(artifact));
        bool stored = false;
        if(m_registry.contains(handle))
            stored = m_registry.replace_asset(handle, version);
        else
            stored = m_registry.register_asset(handle, version);
        if(!stored) {
            LOG_WARN("Shader program {} cannot replace the registered asset", handle.value());
            return ImportPublication::success(std::nullopt);
        }
        return ImportPublication::success(handle);
    }

    AssetManager::ImportPublication AssetManager::publish_mesh_candidate(
        MeshArtifactCandidate& candidate) {
        if(!candidate.result) {
            LOG_ERROR("Failed to prepare mesh artifact '{}' (handle {}): {}",
                candidate.relative_path.generic_string(), candidate.handle.value(),
                candidate.result.error());
            return ImportPublication::success(std::nullopt);
        }
        auto& artifact = candidate.result.value();
        if(candidate.reused_artifact) {
            complete_mesh_import(
                candidate.handle, candidate.revision, artifact.source_dependencies());
            return ImportPublication::success(std::nullopt);
        }
        if(auto publication =
                artifact.publish_atomic(m_import_service->mesh_artifact_path(candidate.handle));
            !publication) {
            LOG_ERROR("Failed to publish mesh artifact '{}' (handle {}): {}",
                candidate.relative_path.generic_string(), candidate.handle.value(),
                publication.error());
            return ImportPublication::success(std::nullopt);
        }
        complete_mesh_import(candidate.handle, candidate.revision, artifact.source_dependencies());

        // Artifact 已发布；后续普通 GPU 创建失败不撤销这个事实。
        if(auto refreshed =
                refresh_loaded_mesh(candidate.handle, candidate.revision, artifact.data);
            !refreshed) {
            if(is_device_lost(refreshed.error()))
                return ImportPublication::failure(refreshed.error());
            LOG_ERROR("Failed to refresh mesh handle {}: {}", candidate.handle.value(),
                refreshed.error().message);
            return ImportPublication::success(candidate.handle);
        }
        LOG_INFO("Imported mesh artifact '{}' (handle {})",
            candidate.relative_path.generic_string(), candidate.handle.value());
        return ImportPublication::success(candidate.handle);
    }

    AssetManager::ImportPublication AssetManager::publish_material_candidate(
        MaterialImportCandidate& candidate) {
        const auto handle = candidate.record.handle;
        if(!candidate.result) {
            LOG_ERROR("Failed to read modified material handle {}: {}", handle.value(),
                candidate.result.error());
            return ImportPublication::success(std::nullopt);
        }
        const auto runtime =
            find_runtime_asset(m_registry, handle, m_render_assets->material(handle));
        if(!runtime.asset)
            return ImportPublication::success(std::nullopt);
        auto material = m_loader->prepare_material(candidate.record, candidate.result.value());
        if(!material) {
            if(is_device_lost(material.error()))
                return ImportPublication::failure(material.error());
            LOG_ERROR(
                "Failed to refresh material {}: {}", handle.value(), material.error().message);
            return ImportPublication::success(std::nullopt);
        }
        if(!m_database.is_current(handle, candidate.revision))
            return ImportPublication::success(std::nullopt);
        if(auto publication =
                publish_material(handle, candidate.result.value(), material.value(), true);
            !publication) {
            LOG_ERROR(
                "Failed to publish material {}: {}", handle.value(), publication.error().message);
            return ImportPublication::success(std::nullopt);
        }
        return ImportPublication::success(handle);
    }

    AssetManager::ImportPublication AssetManager::publish_texture_candidate(
        TextureImportCandidate& candidate) {
        if(!candidate.result) {
            LOG_ERROR("Failed to prepare texture asset '{}' (handle {}): {}",
                candidate.relative_path.generic_string(), candidate.handle.value(),
                candidate.result.error());
            return ImportPublication::success(std::nullopt);
        }
        auto texture_attempt = m_render_assets->prepare(candidate.result.value());
        if(!texture_attempt) {
            if(is_device_lost(texture_attempt.error()))
                return ImportPublication::failure(texture_attempt.error());
            LOG_ERROR("Failed to create refreshed runtime texture for asset handle {}: {}",
                candidate.handle.value(), texture_attempt.error().message);
            return ImportPublication::success(std::nullopt);
        }
        auto texture = std::move(texture_attempt).value();
        if(!m_database.is_current(candidate.handle, candidate.revision)) {
            LOG_DEBUG("Discarded stale runtime texture candidate for asset handle {} (revision {})",
                candidate.handle.value(), candidate.revision);
            return ImportPublication::success(std::nullopt);
        }
        const bool published = m_render_assets->publish(candidate.handle, texture, true);
        if(!published) {
            LOG_ERROR("Failed to publish refreshed runtime texture for asset handle {}",
                candidate.handle.value());
            return ImportPublication::success(std::nullopt);
        }
        if(auto refreshed = reload_loaded_material_dependents(candidate.handle); !refreshed)
            return ImportPublication::failure(refreshed.error());
        LOG_INFO("Published texture asset '{}' (handle {})",
            candidate.relative_path.generic_string(), candidate.handle.value());
        return ImportPublication::success(candidate.handle);
    }

    AssetManager::ImportPublication AssetManager::publish_environment_candidate(
        EnvironmentImportCandidate& candidate) {
        m_environment_previews.erase(candidate.handle);
        m_failed_environments[candidate.handle] = candidate.revision;
        // 失败时移除临时预览，不把它当成最后一个成功版本。
        const auto discard_preview = [this, &candidate] {
            const auto current = m_render_assets->environment(candidate.handle);
            if(current && !m_render_assets->has_lighting(current))
                static_cast<void>(m_registry.unregister_asset(candidate.handle));
        };
        if(!candidate.result) {
            discard_preview();
            LOG_ERROR("Failed to prepare environment '{}': {}",
                candidate.relative_path.generic_string(), candidate.result.error());
            return ImportPublication::success(std::nullopt);
        }
        auto environment = m_render_assets->prepare(candidate.result.value());
        if(!environment) {
            discard_preview();
            if(is_device_lost(environment.error()))
                return ImportPublication::failure(environment.error());
            LOG_ERROR("Failed to create environment {}: {}", candidate.handle.value(),
                environment.error().message);
            return ImportPublication::success(std::nullopt);
        }
        if(!m_database.is_current(candidate.handle, candidate.revision))
            return ImportPublication::success(std::nullopt);
        bool published = false;
        if(m_registry.contains(candidate.handle))
            published = m_render_assets->publish(candidate.handle, environment.value(), true);
        else
            published = m_render_assets->publish(candidate.handle, environment.value(), false);
        if(!published) {
            LOG_ERROR("Failed to publish environment {}", candidate.handle.value());
            return ImportPublication::success(std::nullopt);
        }
        m_failed_environments.erase(candidate.handle);
        LOG_INFO("Published environment asset '{}' (handle {})",
            candidate.relative_path.generic_string(), candidate.handle.value());
        return ImportPublication::success(candidate.handle);
    }

    Result<void, Error> AssetManager::import_mesh(const AssetHandle handle) {
        if(!validate_asset_handle(handle, "import a mesh")) {
            return Result<void, Error>::failure({"Invalid mesh handle"});
        }

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Mesh);
        if(!record) {
            return Result<void, Error>::failure({"Mesh asset record not found"});
        }
        const AssetRevision revision = m_database.get_revision(handle);
        const AssetRecord snapshot = *record;

        if(m_task_queue->contains(handle, revision)) {
            LOG_WARN("Mesh import is already running for handle {}", handle.value());
            return Result<void, Error>::failure({"Mesh import is already running"});
        }
        if(auto artifact = m_import_service->find_current_mesh_artifact(
               handle, snapshot.path, m_limits.mesh_working_bytes)) {
            complete_mesh_import(handle, revision, artifact->source_dependencies());
            LOG_DEBUG("Mesh artifact is current '{}' (handle {})", snapshot.path.generic_string(),
                handle.value());
            return Result<void, Error>::success();
        }

        auto imported = m_import_service->build_mesh_artifact(
            handle, snapshot.path, m_limits.mesh_working_bytes);
        if(!imported) {
            LOG_ERROR("{}", imported.error());
            return Result<void, Error>::failure({imported.error()});
        }
        auto& artifact = imported.value();
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale mesh import for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<void, Error>::failure({"Mesh import revision is stale"});
        }

        if(auto result = artifact.publish_atomic(m_import_service->mesh_artifact_path(handle));
            !result) {
            LOG_ERROR("Failed to publish mesh artifact '{}' (handle {}): {}",
                snapshot.path.generic_string(), handle.value(), result.error());
            return Result<void, Error>::failure({result.error()});
        }
        complete_mesh_import(handle, revision, artifact.source_dependencies());

        if(auto refreshed = refresh_loaded_mesh(handle, revision, artifact.data); !refreshed)
            return Result<void, Error>::failure(refreshed.error());

        LOG_INFO("Imported mesh artifact '{}' (handle {})", snapshot.path.generic_string(),
            handle.value());
        return Result<void, Error>::success();
    }

    Result<void, Error> AssetManager::refresh_loaded_mesh(
        const AssetHandle handle, const AssetRevision revision, const MeshData& data) {
        const auto runtime = find_runtime_asset(m_registry, handle, m_render_assets->mesh(handle));
        if(runtime.type_conflict)
            return Result<void, Error>::failure({"Runtime mesh type conflict"});
        if(!runtime.asset)
            return Result<void, Error>::success();

        auto candidate = m_render_assets->prepare(data);
        if(!candidate)
            return Result<void, Error>::failure(candidate.error());
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale runtime mesh candidate for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<void, Error>::failure({"Runtime mesh revision is stale"});
        }
        if(!m_render_assets->publish(handle, candidate.value(), true))
            return Result<void, Error>::failure({"Failed to publish runtime mesh"});
        return Result<void, Error>::success();
    }

    bool AssetManager::import_mesh_async(const AssetHandle handle, const MeshImportMode mode) {
        const auto* record = find_asset_record(m_database, handle, AssetType::Mesh);
        return record && schedule_mesh_task(*record, mode);
    }

    bool AssetManager::is_mesh_import_pending(const AssetHandle handle) const {
        return m_task_queue->contains(handle, m_database.get_revision(handle));
    }

    std::vector<AssetHandle> AssetManager::mesh_imports_needing_recheck() const {
        return {m_mesh_imports_needing_recheck.begin(), m_mesh_imports_needing_recheck.end()};
    }

    Result<std::shared_ptr<Mesh>, Error> AssetManager::load_mesh(const AssetHandle handle) {
        return m_loader->load_mesh(handle);
    }

    Result<std::shared_ptr<Texture>, Error> AssetManager::load_texture(const AssetHandle handle) {
        return m_loader->load_texture(handle);
    }

    Result<std::shared_ptr<ShaderProgramArtifact>, Error> AssetManager::load_shader_program(
        const AssetHandle handle) {
        return m_loader->load_shader_program(handle);
    }

    Result<std::shared_ptr<AudioClip>, Error> AssetManager::load_audio(const AssetHandle handle) {
        return m_loader->load_audio(handle);
    }

    Result<std::shared_ptr<Environment>, Error> AssetManager::load_environment(
        const AssetHandle handle) {
        const auto state = environment_state(handle);
        if(!state)
            return Result<std::shared_ptr<Environment>, Error>::failure(state.error());
        if(state.value() == EnvironmentState::Preparing)
            return Result<std::shared_ptr<Environment>, Error>::failure(
                {"Environment is still preparing; use request_load and references_ready"});
        if(state.value() == EnvironmentState::Failed)
            return Result<std::shared_ptr<Environment>, Error>::failure(
                {"Environment preparation failed; waiting for source changes"});
        return m_loader->load_environment(handle);
    }

    Result<std::shared_ptr<Texture>, Error> AssetManager::reimport_texture(
        const AssetHandle handle, TextureImportSettings import_settings) {
        if(!validate_asset_handle(handle, "reimport a texture")) {
            return Result<std::shared_ptr<Texture>, Error>::failure({"Invalid texture handle"});
        }

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Texture);
        if(!record) {
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Texture is not indexed with the expected type"});
        }

        const auto runtime =
            find_runtime_asset(m_registry, handle, m_render_assets->texture(handle));
        if(runtime.type_conflict) {
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Runtime texture type conflict"});
        }
        const auto& previous_texture = runtime.asset;

        const AssetRevision revision = m_database.get_revision(handle);
        const AssetRecord snapshot = *record;
        auto texture = m_loader->prepare_texture(snapshot, import_settings);
        if(!texture)
            return texture;
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale texture reimport for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Texture changed during reimport"});
        }

        if(auto updated = m_database.update_import_settings(handle, import_settings); !updated) {
            return Result<std::shared_ptr<Texture>, Error>::failure({updated.error()});
        }

        bool published;
        if(previous_texture)
            published = m_render_assets->publish(handle, texture.value(), true);
        else
            published = m_render_assets->publish(handle, texture.value(), false);
        if(!published) {
            return Result<std::shared_ptr<Texture>, Error>::failure(
                {"Failed to publish runtime texture"});
        }

        if(auto refreshed = reload_loaded_material_dependents(handle); !refreshed)
            return Result<std::shared_ptr<Texture>, Error>::failure(refreshed.error());
        LOG_INFO("Reimported texture asset '{}' (handle {}, color_space={}, flip_y={})",
            snapshot.path.generic_string(), handle.value(), to_string(import_settings.color_space),
            import_settings.flip_y);
        return texture;
    }

    Result<void, Error> AssetManager::reload_loaded_material_dependents(
        const AssetHandle texture_handle) {
        const auto dependents = m_database.get_dependents(texture_handle);
        // 重载会修改依赖索引，先复制句柄。
        const std::vector<AssetHandle> snapshot(dependents.begin(), dependents.end());
        for(const auto handle : snapshot) {
            const auto* record = m_database.find(handle);
            if(!record || record->type != AssetType::Material || !m_render_assets->material(handle))
                continue;
            if(auto refreshed = reload_material(handle); !refreshed) {
                if(is_device_lost(refreshed.error()))
                    return Result<void, Error>::failure(refreshed.error());
                LOG_ERROR("Texture {} updated, but dependent material {} failed: {}",
                    texture_handle.value(), handle.value(), refreshed.error().message);
            }
        }
        return Result<void, Error>::success();
    }

    Result<std::shared_ptr<Material>, Error> AssetManager::load_material(const AssetHandle handle) {
        return m_loader->load_material(handle);
    }

    Result<std::shared_ptr<Material>, Error> AssetManager::reload_material(
        const AssetHandle handle) {
        if(!validate_asset_handle(handle, "reload a material")) {
            return Result<std::shared_ptr<Material>, Error>::failure({"Invalid material handle"});
        }

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Material);
        if(!record) {
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Material is not indexed with the expected type"});
        }

        const auto runtime =
            find_runtime_asset(m_registry, handle, m_render_assets->material(handle));
        if(runtime.type_conflict) {
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Runtime material type conflict"});
        }
        const bool has_runtime_asset = static_cast<bool>(runtime.asset);

        const AssetRevision revision = m_database.get_revision(handle);
        const AssetRecord snapshot = *record;
        const auto data = MaterialSerializer{}.load(m_database.paths().assets() / snapshot.path);
        if(!data) {
            return Result<std::shared_ptr<Material>, Error>::failure({data.error()});
        }
        auto material = m_loader->prepare_material(snapshot, data.value());
        if(!material)
            return material;
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale material reload for asset handle {} (revision {})",
                handle.value(), revision);
            return Result<std::shared_ptr<Material>, Error>::failure(
                {"Material changed during reload"});
        }
        if(auto published =
                publish_material(handle, data.value(), material.value(), has_runtime_asset);
            !published)
            return Result<std::shared_ptr<Material>, Error>::failure(published.error());
        LOG_INFO("Reloaded material asset '{}' (handle {})", snapshot.path.generic_string(),
            handle.value());
        return material;
    }

    std::shared_ptr<const Material> AssetManager::MaterialUpdate::material() const {
        return m_material;
    }

    Result<AssetManager::MaterialUpdate, Error> AssetManager::prepare_material_update(
        const AssetHandle handle, const MaterialData& data) {
        using Preparation = Result<MaterialUpdate, Error>;
        if(!validate_asset_handle(handle, "update a material")) {
            return Preparation::failure({"Invalid material handle"});
        }

        const AssetRecord* record = find_asset_record(m_database, handle, AssetType::Material);
        if(!record) {
            return Preparation::failure({"Material is not indexed with the expected type"});
        }

        const auto runtime =
            find_runtime_asset(m_registry, handle, m_render_assets->material(handle));
        if(runtime.type_conflict) {
            return Preparation::failure({"Runtime material type conflict"});
        }

        const AssetRevision revision = m_database.get_revision(handle);
        const AssetRecord snapshot = *record;
        if(auto valid = MaterialSerializer{}.validate(data); !valid)
            return Preparation::failure({valid.error()});
        auto material = m_loader->prepare_material(snapshot, data);
        if(!material)
            return Preparation::failure(material.error());
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG("Discarded stale material update for asset handle {} (revision {})",
                handle.value(), revision);
            return Preparation::failure({"Material changed during update"});
        }
        MaterialUpdate update;
        update.m_owner = this;
        update.m_record = snapshot;
        update.m_revision = revision;
        update.m_data = data;
        update.m_material = std::move(material).value();
        update.m_expected = runtime.asset;
        return Preparation::success(std::move(update));
    }

    Result<std::shared_ptr<Material>, Error> AssetManager::commit_material_update(
        const MaterialUpdate& update) {
        const auto handle = update.handle();
        const auto runtime =
            find_runtime_asset(m_registry, handle, m_render_assets->material(handle));
        if(update.m_owner != this || !update.m_material
            || !m_database.is_current(handle, update.m_revision) || runtime.type_conflict
            || runtime.asset != update.m_expected)
            return Result<std::shared_ptr<Material>, Error>::failure({"Material update is stale"});
        const auto serialized = MaterialSerializer{}.serialize(update.m_data);
        if(!serialized)
            return Result<std::shared_ptr<Material>, Error>::failure({serialized.error()});
        if(auto saved = write_text_file_atomic(
               m_database.paths().assets() / update.m_record.path, serialized.value());
            !saved) {
            return Result<std::shared_ptr<Material>, Error>::failure({saved.error()});
        }
        if(auto published = publish_material(
               handle, update.m_data, update.m_material, static_cast<bool>(update.m_expected));
            !published)
            return Result<std::shared_ptr<Material>, Error>::failure(published.error());
        LOG_INFO("Updated material asset '{}' (handle {})", update.m_record.path.generic_string(),
            handle.value());
        return Result<std::shared_ptr<Material>, Error>::success(update.m_material);
    }

    Result<void, Error> AssetManager::preview_material_update(MaterialUpdate& update) {
        if(update.m_owner != this || !update.m_material
            || !m_database.is_current(update.handle(), update.m_revision)
            || m_render_assets->material(update.handle()) != update.m_expected)
            return Result<void, Error>::failure({"Material preview is stale"});
        if(!m_render_assets->publish(update.handle(), update.m_material, true))
            return Result<void, Error>::failure({"Cannot publish material preview"});
        update.m_expected = update.m_material;
        return Result<void, Error>::success();
    }

    Result<void, Error> AssetManager::restore_material_preview(
        const MaterialUpdate& update, const std::shared_ptr<Material>& previous) {
        if(update.m_owner != this || !previous
            || m_render_assets->material(update.handle()) != update.m_material)
            return Result<void, Error>::failure({"Material preview was replaced"});
        if(!m_render_assets->publish(update.handle(), previous, true))
            return Result<void, Error>::failure({"Cannot restore material preview"});
        return Result<void, Error>::success();
    }

    Result<void, Error> AssetManager::publish_material(const AssetHandle handle,
        const MaterialData& data, const std::shared_ptr<Material>& material,
        const bool replace_existing) {
        if(auto updated = m_database.update_dependencies(handle, get_asset_dependencies(data));
            !updated)
            return Result<void, Error>::failure({updated.error()});
        bool published;
        if(replace_existing)
            published = m_render_assets->publish(handle, material, true);
        else
            published = m_render_assets->publish(handle, material, false);
        if(!published)
            return Result<void, Error>::failure({"Failed to publish material"});
        return Result<void, Error>::success();
    }

}
