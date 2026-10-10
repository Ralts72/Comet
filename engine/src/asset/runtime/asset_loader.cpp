#include "asset/runtime/asset_loader.h"

#include "asset/artifact/mesh_artifact.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/database.h"
#include "asset/import/import_service.h"
#include "asset/registry.h"
#include "asset/runtime/render_asset_publisher.h"
#include "asset/serialization/material_serializer.h"
#include "audio/audio.h"
#include "diagnostics/logger.h"

#include <map>
#include <utility>

namespace Comet {
    namespace {
        template<typename T, typename Resolve, typename Create, typename Publish>
        Result<std::shared_ptr<T>, Error> load_runtime_asset(const AssetDatabase& database,
            AssetRegistry& registry, const AssetHandle handle, const AssetType type,
            Resolve&& resolve, Create&& create, Publish&& publish) {
            using LoadResult = Result<std::shared_ptr<T>, Error>;
            const auto* record = database.find(handle);
            if(!record || record->type != type)
                return LoadResult::failure({"Asset is not indexed with the expected type: "
                                            + std::to_string(handle.value())});
            if(auto asset = resolve())
                return LoadResult::success(std::move(asset));
            if(registry.contains(handle))
                return LoadResult::failure({"Runtime asset type conflict"});
            const AssetRevision revision = database.get_revision(handle);
            // 创建依赖前复制记录；借用不能跨越可能修改数据库的调用。
            const AssetRecord snapshot = *record;
            auto candidate = create(snapshot);
            if(!candidate)
                return candidate;
            if(!database.is_current(handle, revision))
                return LoadResult::failure({"Asset changed during loading"});
            if(!publish(candidate.value()))
                return LoadResult::failure({"Failed to publish runtime asset"});
            return candidate;
        }
    }

    AssetLoader::AssetLoader(AssetDatabase& database, AssetRegistry& registry,
        ImportService& imports, RenderAssetPublisher& render_assets, std::size_t mesh_bytes,
        std::size_t texture_bytes, std::size_t environment_bytes)
        : m_database(database), m_registry(registry), m_imports(imports),
          m_render_assets(render_assets), m_mesh_bytes(mesh_bytes), m_texture_bytes(texture_bytes),
          m_environment_bytes(environment_bytes) {}

    Result<std::shared_ptr<Mesh>, Error> AssetLoader::load_mesh(const AssetHandle handle) {
        return load_runtime_asset<Mesh>(
            m_database, m_registry, handle, AssetType::Mesh,
            [this, handle] { return m_render_assets.mesh(handle); },
            [this](const AssetRecord& record) -> Result<std::shared_ptr<Mesh>, Error> {
                auto artifact = MeshArtifact::load(
                    m_imports.mesh_artifact_path(record.handle), record.handle, m_mesh_bytes);
                if(!artifact)
                    return Result<std::shared_ptr<Mesh>, Error>::failure(
                        {"Mesh artifact is missing or invalid; import the asset before loading: "
                            + record.path.generic_string()});
                if(auto updated = m_database.update_import_dependencies(
                       record.handle, artifact->source_dependencies());
                    !updated)
                    LOG_WARN("Could not index import dependencies for asset handle {}: {}",
                        record.handle.value(), updated.error());
                return m_render_assets.prepare(artifact->data);
            },
            [this, handle](
                const auto& asset) { return m_render_assets.publish(handle, asset, false); });
    }

    Result<std::shared_ptr<Texture>, Error> AssetLoader::load_texture(const AssetHandle handle) {
        return load_runtime_asset<Texture>(
            m_database, m_registry, handle, AssetType::Texture,
            [this, handle] { return m_render_assets.texture(handle); },
            [this](const AssetRecord& record) -> Result<std::shared_ptr<Texture>, Error> {
                const auto* settings = std::get_if<TextureImportSettings>(&record.import_settings);
                if(!settings) {
                    return Result<std::shared_ptr<Texture>, Error>::failure(
                        {"Texture asset has incompatible import settings"});
                }
                return prepare_texture(record, *settings);
            },
            [this, handle](
                const auto& asset) { return m_render_assets.publish(handle, asset, false); });
    }

    Result<std::shared_ptr<ShaderProgramArtifact>, Error> AssetLoader::load_shader_program(
        const AssetHandle handle) {
        return load_runtime_asset<ShaderProgramArtifact>(
            m_database, m_registry, handle, AssetType::ShaderProgram,
            [this, handle] { return m_registry.resolve<ShaderProgramArtifact>(handle); },
            [this, handle](const AssetRecord&) {
                auto artifact = ShaderProgramArtifact::load(
                    m_imports.shader_program_artifact_path(handle), handle);
                if(!artifact
                    || !import_inputs_are_current(m_database.paths().assets(), artifact->inputs))
                    return Result<std::shared_ptr<ShaderProgramArtifact>, Error>::failure(
                        {"Current compiled Shader program artifact is unavailable"});
                return Result<std::shared_ptr<ShaderProgramArtifact>, Error>::success(
                    std::make_shared<ShaderProgramArtifact>(std::move(*artifact)));
            },
            [this, handle](const auto& asset) { return m_registry.register_asset(handle, asset); });
    }

    Result<std::shared_ptr<AudioClip>, Error> AssetLoader::load_audio(const AssetHandle handle) {
        return load_runtime_asset<AudioClip>(
            m_database, m_registry, handle, AssetType::Audio,
            [this, handle] { return m_registry.resolve<AudioClip>(handle); },
            [this](const AssetRecord& record) -> Result<std::shared_ptr<AudioClip>, Error> {
                auto path = m_database.paths().resolve_asset_path(record.path);
                if(!path)
                    return Result<std::shared_ptr<AudioClip>, Error>::failure({path.error()});
                return AudioClip::load(path.value());
            },
            [this, handle](const auto& asset) { return m_registry.register_asset(handle, asset); });
    }

    Result<std::shared_ptr<Material>, Error> AssetLoader::load_material(const AssetHandle handle) {
        return load_runtime_asset<Material>(
            m_database, m_registry, handle, AssetType::Material,
            [this, handle] { return m_render_assets.material(handle); },
            [this](const AssetRecord& record) -> Result<std::shared_ptr<Material>, Error> {
                const auto data =
                    MaterialSerializer{}.load(m_database.paths().assets() / record.path);
                if(!data)
                    return Result<std::shared_ptr<Material>, Error>::failure({data.error()});
                return prepare_material(record, data.value());
            },
            [this, handle](
                const auto& asset) { return m_render_assets.publish(handle, asset, false); });
    }

    Result<std::shared_ptr<Environment>, Error> AssetLoader::load_environment(
        const AssetHandle handle) {
        return load_runtime_asset<Environment>(
            m_database, m_registry, handle, AssetType::Environment,
            [this, handle] { return m_render_assets.environment(handle); },
            [this](const AssetRecord& record) -> Result<std::shared_ptr<Environment>, Error> {
                auto prepared = m_imports.prepare_environment(record, m_environment_bytes);
                if(!prepared)
                    return Result<std::shared_ptr<Environment>, Error>::failure({prepared.error()});
                return m_render_assets.prepare(prepared.value().data);
            },
            [this, handle](
                const auto& asset) { return m_render_assets.publish(handle, asset, false); });
    }

    Result<std::shared_ptr<Texture>, Error> AssetLoader::prepare_texture(
        const AssetRecord& record, const TextureImportSettings& import_settings) {
        auto data = m_imports.prepare_texture(record, import_settings, m_texture_bytes);
        if(!data)
            return Result<std::shared_ptr<Texture>, Error>::failure({data.error()});
        return m_render_assets.prepare(data.value());
    }

    Result<std::shared_ptr<Material>, Error> AssetLoader::prepare_material(
        const AssetRecord& record, const MaterialData& data) {
        if(data.shader_program) {
            const auto* program = m_database.find(data.shader_program);
            if(!program || program->type != AssetType::ShaderProgram)
                return Result<std::shared_ptr<Material>, Error>::failure(
                    {"Material '" + record.path.generic_string()
                        + "' references a missing or non-program Shader asset"});
            if(auto loaded = load_shader_program(data.shader_program); !loaded)
                return Result<std::shared_ptr<Material>, Error>::failure(
                    {"Material '" + record.path.generic_string() + "': " + loaded.error().message});
        }
        std::map<std::string, std::shared_ptr<Texture>> textures;
        for(const auto& [property_name, texture_handle] : data.texture_properties) {
            auto texture = load_texture(texture_handle);
            if(!texture) {
                auto error = texture.error();
                error.message = "Material '" + record.path.generic_string() + "' property '"
                                + property_name + "': " + error.message;
                return Result<std::shared_ptr<Material>, Error>::failure(std::move(error));
            }
            textures.emplace(property_name, std::move(texture).value());
        }

        return m_render_assets.prepare_material(record.path.stem().string(), data, textures);
    }
}
