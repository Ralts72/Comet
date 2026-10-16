#include "asset/runtime/asset_loader.h"
#include "asset/data/animation_library.h"
#include "asset/data/game_data.h"
#include "asset/data/irradiance_data.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "asset/default_assets.h"
#include "asset/import/mesh_importer.h"
#include "scene/prefab.h"

#include "asset/artifact/mesh_artifact.h"
#include "asset/artifact/environment_artifact.h"
#include "asset/artifact/model_artifact.h"
#include "asset/artifact/texture_artifact.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/database.h"
#include "asset/import/import_service.h"
#include "asset/registry.h"
#include "asset/runtime/render_asset_factory.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "audio/audio.h"
#include "diagnostics/logger.h"

#include <map>
#include <utility>

namespace Comet {
    namespace {
        template<typename T, typename Create>
        Result<std::shared_ptr<T>, Error> load_default_asset(
            AssetRegistry& registry, const AssetHandle handle, Create&& create) {
            using LoadResult = Result<std::shared_ptr<T>, Error>;
            if(auto asset = registry.resolve<T>(handle))
                return LoadResult::success(std::move(asset));
            if(registry.contains(handle))
                return LoadResult::failure({"Default asset type conflict"});
            auto candidate = create();
            if(!candidate)
                return candidate;
            if(!registry.register_asset(handle, candidate.value()))
                return LoadResult::failure({"Cannot register default asset"});
            return candidate;
        }

        template<typename T, typename Create>
        Result<std::shared_ptr<T>, Error> load_runtime_asset(const AssetDatabase& database,
            AssetRegistry& registry, const AssetHandle handle, const AssetType type,
            Create&& create) {
            using LoadResult = Result<std::shared_ptr<T>, Error>;
            const auto* record = database.find(handle);
            if(!record || record->type != type)
                return LoadResult::failure({"Asset is not indexed with the expected type: "
                                            + std::to_string(handle.value())});
            if(auto asset = registry.resolve<T>(handle))
                return LoadResult::success(std::move(asset));
            if(registry.contains(handle))
                return LoadResult::failure({"Runtime asset type conflict"});
            const AssetRevision revision = database.get_revision(handle);
            const auto publication = registry.get_revision(handle);
            // 创建依赖前复制记录；借用不能跨越可能修改数据库的调用。
            const AssetRecord snapshot = *record;
            auto candidate = create(snapshot);
            if(!candidate)
                return candidate;
            // Main 接纳时复核源与运行发布版本；相同对象重新发布也使旧请求过期。
            if(!database.is_current(handle, revision)
                || registry.get_revision(handle) != publication)
                return LoadResult::failure({"Asset changed during loading"});
            if(!registry.register_asset(handle, candidate.value()))
                return LoadResult::failure({"Failed to publish runtime asset"});
            return candidate;
        }
    }

    AssetLoader::AssetLoader(AssetDatabase& database, AssetRegistry& registry,
        ImportService& imports, RenderAssetFactory& render_factory, std::size_t mesh_bytes,
        std::size_t texture_bytes, std::size_t environment_bytes)
        : m_database(database), m_registry(registry), m_imports(imports),
          m_render_factory(render_factory), m_mesh_bytes(mesh_bytes),
          m_texture_bytes(texture_bytes), m_environment_bytes(environment_bytes) {}

    Result<std::shared_ptr<const ModelArtifact>, Error> AssetLoader::model_artifact(
        const AssetHandle handle) {
        using Loaded = Result<std::shared_ptr<const ModelArtifact>, Error>;
        const auto* record = m_database.find(handle);
        if(!record || record->type != AssetType::Model || record->source_asset)
            return Loaded::failure({"Model is not indexed with the expected type"});
        const auto revision = m_database.get_revision(handle);
        if(const auto cached = m_models.find(handle);
            cached != m_models.end() && cached->second.revision == revision)
            return Loaded::success(cached->second.artifact);
        m_models.erase(handle);
        auto artifact = ModelArtifact::load(m_imports.model_artifact_path(handle), handle);
        if(!artifact || artifact->importer_version != ModelArtifact::IMPORTER_VERSION
            || artifact->source_inputs.files.front().relative_path
                   != record->path.lexically_normal()
            || !import_inputs_are_current(m_database.paths().assets(), artifact->source_inputs))
            return Loaded::failure(
                {"Model artifact is missing or stale; import the model before loading: "
                    + record->path.generic_string()});
        const auto metadata =
            MetadataSerializer{}.load(metadata_path(m_database.paths().assets() / record->path));
        if(!metadata || metadata.value() != artifact->metadata)
            return Loaded::failure({"Model artifact metadata is stale"});
        std::unordered_map<AssetHandle, std::size_t> children;
        for(const auto& child : artifact->metadata.subresources) {
            const auto* indexed = m_database.find(child.handle);
            if(!indexed || indexed->source_asset != handle || indexed->source_key != child.key
                || indexed->type != child.type)
                return Loaded::failure(
                    {"Model products do not match the indexed child identities"});
            children.emplace(child.handle, children.size());
        }
        // 伴随文件归源模型所有；其变更由数据库传播至全部子资源。
        std::vector<std::filesystem::path> dependencies;
        for(const auto& input : artifact->source_inputs.files)
            dependencies.push_back(input.relative_path);
        if(auto updated = m_database.update_import_dependencies(handle, std::move(dependencies));
            !updated)
            return Loaded::failure({updated.error()});
        auto shared = std::make_shared<const ModelArtifact>(std::move(*artifact));
        m_models.insert_or_assign(handle, CachedModel{revision, shared, std::move(children)});
        return Loaded::success(std::move(shared));
    }

    Result<std::shared_ptr<AnimationLibrary>, Error> AssetLoader::load_animation_library(
        AssetHandle handle) {
        using Loaded = Result<std::shared_ptr<AnimationLibrary>, Error>;
        return load_runtime_asset<AnimationLibrary>(m_database, m_registry, handle,
            AssetType::AnimationLibrary, [&](const AssetRecord& record) -> Loaded {
                auto library = AnimationLibrary::load(m_database.paths().assets() / record.path);
                if(!library)
                    return Loaded::failure({library.error()});
                for(const auto& entry : library.value().entries) {
                    if(const auto found = library.value().sources.find(entry.mesh);
                        found != library.value().sources.end()) {
                        if(!found->second->find_clip(entry.clip))
                            return Loaded::failure(
                                {"Animation source has no requested clip: " + entry.clip});
                        continue;
                    }
                    auto mesh = read_mesh_data(entry.mesh);
                    if(!mesh)
                        return Loaded::failure(mesh.error());
                    if(!mesh.value().animation || !mesh.value().animation->find_clip(entry.clip))
                        return Loaded::failure(
                            {"Animation source has no requested clip: " + entry.clip});
                    library.value().sources.emplace(entry.mesh, mesh.value().animation);
                }
                return Loaded::success(
                    std::make_shared<AnimationLibrary>(std::move(library).value()));
            });
    }
    Result<std::shared_ptr<AnimationController>, Error> AssetLoader::load_animation_controller(
        AssetHandle handle) {
        using Loaded = Result<std::shared_ptr<AnimationController>, Error>;
        const auto load = [this](const std::filesystem::path& path) -> Loaded {
            auto data = AnimationControllerSerializer{}.load(path);
            if(!data)
                return Loaded::failure({data.error()});
            if(data.value().library) {
                auto library = load_animation_library(data.value().library);
                if(!library)
                    return Loaded::failure(library.error());
            }
            return Loaded::success(std::make_shared<AnimationController>(std::move(data).value()));
        };
        if(handle == DEFAULT_CHARACTER_ANIMATION && !m_database.is_read_only())
            return load_default_asset<AnimationController>(m_registry, handle,
                [&] { return load(default_character_source().parent_path() / "default.ani"); });
        return load_runtime_asset<AnimationController>(m_database, m_registry, handle,
            AssetType::AnimationController, [&](const AssetRecord& record) -> Loaded {
                auto path = m_database.paths().resolve_asset_path(record.path);
                if(!path)
                    return Loaded::failure({path.error()});
                return load(path.value());
            });
    }

    Result<std::shared_ptr<GameData>, Error> AssetLoader::load_data(AssetHandle handle) {
        return load_runtime_asset<GameData>(m_database, m_registry, handle, AssetType::Data,
            [&](const AssetRecord& record) -> Result<std::shared_ptr<GameData>, Error> {
                auto path = m_database.paths().resolve_asset_path(record.path);
                if(!path)
                    return Result<std::shared_ptr<GameData>, Error>::failure({path.error()});
                auto data = GameData::load(path.value());
                if(!data)
                    return Result<std::shared_ptr<GameData>, Error>::failure({data.error()});
                return Result<std::shared_ptr<GameData>, Error>::success(
                    std::make_shared<GameData>(std::move(data).value()));
            });
    }

    Result<std::shared_ptr<Prefab>, Error> AssetLoader::load_prefab(
        AssetHandle handle, const ComponentRegistry& components) {
        return load_runtime_asset<Prefab>(m_database, m_registry, handle, AssetType::Prefab,
            [&](const AssetRecord& record) -> Result<std::shared_ptr<Prefab>, Error> {
                auto path = m_database.paths().resolve_asset_path(record.path);
                if(!path)
                    return Result<std::shared_ptr<Prefab>, Error>::failure({path.error()});
                auto prefab = Prefab::load(path.value(), components);
                if(!prefab)
                    return Result<std::shared_ptr<Prefab>, Error>::failure({prefab.error()});
                return Result<std::shared_ptr<Prefab>, Error>::success(
                    std::make_shared<Prefab>(std::move(prefab).value()));
            });
    }

    Result<std::shared_ptr<const ModelData>, Error> AssetLoader::load_model(AssetHandle handle) {
        auto artifact = model_artifact(handle);
        if(!artifact)
            return Result<std::shared_ptr<const ModelData>, Error>::failure(artifact.error());
        const auto* data = &artifact.value()->model;
        return Result<std::shared_ptr<const ModelData>, Error>::success(
            std::shared_ptr<const ModelData>(std::move(artifact).value(), data));
    }

    bool AssetLoader::model_is_loaded(AssetHandle handle) const {
        const auto cached = m_models.find(handle);
        return cached != m_models.end() && m_database.is_current(handle, cached->second.revision);
    }

    void AssetLoader::invalidate_model(AssetHandle handle) {
        m_models.erase(handle);
    }

    std::size_t AssetLoader::release_models_except(
        const std::unordered_set<AssetHandle>& retained) {
        return std::erase_if(
            m_models, [&](const auto& entry) { return !retained.contains(entry.first); });
    }

    Result<std::filesystem::path, Error> AssetLoader::product_path(
        const AssetRecord& record, const std::filesystem::path& imported_path) {
        if(m_database.is_read_only()) {
            auto resolved = m_database.paths().resolve_asset_path(record.path);
            if(!resolved)
                return Result<std::filesystem::path, Error>::failure({resolved.error()});
            return Result<std::filesystem::path, Error>::success(std::move(resolved).value());
        }
        if(record.source_asset)
            return model_product_path(record);
        return Result<std::filesystem::path, Error>::success(imported_path);
    }

    bool AssetLoader::product_source_is_current(
        const AssetRecord& record, const ImportInputFingerprint& source) const {
        auto inputs = capture_import_inputs(
            m_database.paths().assets(), m_database.paths().assets() / record.path, {});
        return inputs && inputs.value().files.front() == source;
    }

    Result<std::filesystem::path, Error> AssetLoader::model_product_path(
        const AssetRecord& record) {
        auto artifact = model_artifact(record.source_asset);
        if(!artifact)
            return Result<std::filesystem::path, Error>::failure(artifact.error());
        const auto& children = m_models.at(record.source_asset).children;
        const auto found = children.find(record.handle);
        if(found == children.end())
            return Result<std::filesystem::path, Error>::failure({"Model child identity is stale"});
        const auto& child = artifact.value()->metadata.subresources[found->second];
        if(child.key != record.source_key || child.type != record.type)
            return Result<std::filesystem::path, Error>::failure({"Model child identity is stale"});
        return Result<std::filesystem::path, Error>::success(artifact.value()->product_path(
            m_imports.model_artifact_path(record.source_asset), record.handle));
    }

    Result<MeshData, Error> AssetLoader::read_mesh_data(const AssetHandle handle) {
        if(handle == DEFAULT_CHARACTER_MESH && !m_database.is_read_only()) {
            static const auto imported = MeshImporter{}.import(default_character_source());
            if(!imported)
                return Result<MeshData, Error>::failure({imported.error()});
            return Result<MeshData, Error>::success(imported.value());
        }
        const auto* record = m_database.find(handle);
        if(!record || record->type != AssetType::Mesh)
            return Result<MeshData, Error>::failure(
                {"Mesh is not indexed with the expected type: " + std::to_string(handle.value())});
        // 模型依赖加载可能修改索引，读取期间保留自己的记录。
        const AssetRecord snapshot = *record;
        return read_mesh_data(snapshot);
    }

    Result<MeshData, Error> AssetLoader::read_mesh_data(const AssetRecord& record) {
        auto path = product_path(record, m_imports.mesh_artifact_path(record.handle));
        if(!path)
            return Result<MeshData, Error>::failure(path.error());
        auto artifact = MeshArtifact::load(path.value(), record.handle, m_mesh_bytes);
        if(!artifact)
            return Result<MeshData, Error>::failure(
                {"Mesh artifact is missing or invalid; import the asset before loading: "
                    + record.path.generic_string()});
        if(!record.source_asset && !m_database.is_read_only())
            if(auto updated = m_database.update_import_dependencies(
                   record.handle, artifact->source_dependencies());
                !updated)
                LOG_WARN("Could not index import dependencies for asset handle {}: {}",
                    record.handle.value(), updated.error());
        return Result<MeshData, Error>::success(std::move(artifact->data));
    }

    Result<std::shared_ptr<Mesh>, Error> AssetLoader::load_mesh(const AssetHandle handle) {
        const auto create = [this](Result<MeshData, Error> data) {
            if(!data)
                return Result<std::shared_ptr<Mesh>, Error>::failure(data.error());
            return m_render_factory.prepare(std::move(data).value());
        };
        if(handle == DEFAULT_CHARACTER_MESH)
            return load_default_asset<Mesh>(
                m_registry, handle, [&] { return create(read_mesh_data(handle)); });
        return load_runtime_asset<Mesh>(m_database, m_registry, handle, AssetType::Mesh,
            [&](const AssetRecord& record) { return create(read_mesh_data(record)); });
    }

    Result<TextureData, Error> AssetLoader::read_texture_data(const AssetHandle handle) {
        using Read = Result<TextureData, Error>;
        const auto* indexed = m_database.find(handle);
        if(!indexed || indexed->type != AssetType::Texture)
            return Read::failure({"Texture asset is not indexed"});
        const AssetRecord record = *indexed;
        auto product = product_path(record, m_imports.texture_artifact_path(handle));
        if(!product)
            return Read::failure(product.error());
        auto artifact = TextureArtifact::load(product.value(), handle, m_texture_bytes);
        if(!artifact)
            return Read::failure({"Texture CPU product is not ready"});
        if(m_database.is_read_only()) {
            if(const auto* sampler = m_database.product_sampler(handle))
                artifact->data.sampler = *sampler;
        } else if(record.source_asset) {
            artifact->data.sampler =
                m_models.at(record.source_asset).artifact->texture_samplers.at(handle);
        } else {
            const auto* settings = std::get_if<TextureImportSettings>(&record.import_settings);
            if(!product_source_is_current(record, artifact->source) || !settings
                || artifact->settings != *settings)
                return Read::failure({"Texture CPU product is stale; wait for import"});
        }
        return Read::success(std::move(artifact->data));
    }

    Result<EnvironmentData, Error> AssetLoader::read_environment_data(const AssetRecord& record) {
        using Read = Result<EnvironmentData, Error>;
        auto path = product_path(record, m_imports.environment_artifact_path(record.handle));
        if(!path)
            return Read::failure(path.error());
        auto artifact = EnvironmentArtifact::load(path.value(), record.handle, m_environment_bytes);
        if(!artifact)
            return Read::failure({"Environment CPU product is not ready"});
        if(!m_database.is_read_only() && !product_source_is_current(record, artifact->source))
            return Read::failure({"Environment CPU product is stale; wait for import"});
        return Read::success(std::move(artifact->data));
    }

    Result<TextureData, Error> AssetLoader::read_environment_background(const AssetHandle handle) {
        const auto* record = m_database.find(handle);
        if(!record || record->type != AssetType::Environment)
            return Result<TextureData, Error>::failure({"Environment asset is not indexed"});
        auto data = read_environment_data(*record);
        if(!data)
            return Result<TextureData, Error>::failure(data.error());
        return Result<TextureData, Error>::success(std::move(data).value().background);
    }

    Result<std::shared_ptr<Texture>, Error> AssetLoader::load_texture(const AssetHandle handle) {
        return load_runtime_asset<Texture>(m_database, m_registry, handle, AssetType::Texture,
            [this](const AssetRecord& record) -> Result<std::shared_ptr<Texture>, Error> {
                if(m_database.is_read_only() || record.source_asset) {
                    auto data = read_texture_data(record.handle);
                    if(!data)
                        return Result<std::shared_ptr<Texture>, Error>::failure(data.error());
                    return m_render_factory.prepare(std::move(data).value());
                }
                const auto* settings = std::get_if<TextureImportSettings>(&record.import_settings);
                if(!settings) {
                    return Result<std::shared_ptr<Texture>, Error>::failure(
                        {"Texture asset has incompatible import settings"});
                }
                return prepare_texture(record, *settings);
            });
    }

    Result<std::shared_ptr<ShaderProgramArtifact>, Error> AssetLoader::load_shader_program(
        const AssetHandle handle) {
        return load_runtime_asset<ShaderProgramArtifact>(m_database, m_registry, handle,
            AssetType::ShaderProgram, [this, handle](const AssetRecord& record) {
                auto path = product_path(record, m_imports.shader_program_artifact_path(handle));
                if(!path)
                    return Result<std::shared_ptr<ShaderProgramArtifact>, Error>::failure(
                        path.error());
                auto artifact = ShaderProgramArtifact::load(path.value(), handle);
                if(!artifact
                    || (!m_database.is_read_only()
                        && !import_inputs_are_current(
                            m_database.paths().assets(), artifact->inputs)))
                    return Result<std::shared_ptr<ShaderProgramArtifact>, Error>::failure(
                        {"Current compiled Shader program artifact is unavailable"});
                return Result<std::shared_ptr<ShaderProgramArtifact>, Error>::success(
                    std::make_shared<ShaderProgramArtifact>(std::move(*artifact)));
            });
    }

    Result<std::shared_ptr<AudioClip>, Error> AssetLoader::load_audio(const AssetHandle handle) {
        return load_runtime_asset<AudioClip>(m_database, m_registry, handle, AssetType::Audio,
            [this](const AssetRecord& record) -> Result<std::shared_ptr<AudioClip>, Error> {
                auto path = m_database.paths().resolve_asset_path(record.path);
                if(!path)
                    return Result<std::shared_ptr<AudioClip>, Error>::failure({path.error()});
                const auto* settings = std::get_if<AudioImportSettings>(&record.import_settings);
                return AudioClip::load(path.value(), settings && settings->stream);
            });
    }

    Result<std::shared_ptr<Material>, Error> AssetLoader::load_material(const AssetHandle handle) {
        if(const auto* asset = find_default_asset(handle);
            asset && asset->type == AssetType::Material) {
            return load_default_asset<Material>(m_registry, handle, [asset] {
                // 参数缺省值由 MaterialLayout 定义，不再复制一份 .mat 配置。
                return RenderAssetFactory::prepare_material(std::string(asset->name),
                    MaterialData{.template_name = std::string(asset->material_template)}, {});
            });
        }
        return load_runtime_asset<Material>(m_database, m_registry, handle, AssetType::Material,
            [this](const AssetRecord& record) -> Result<std::shared_ptr<Material>, Error> {
                auto path = product_path(record, m_database.paths().assets() / record.path);
                if(!path)
                    return Result<std::shared_ptr<Material>, Error>::failure(path.error());
                const auto data = MaterialSerializer{}.load(path.value());
                if(!data)
                    return Result<std::shared_ptr<Material>, Error>::failure({data.error()});
                return prepare_material(record, data.value());
            });
    }

    Result<std::shared_ptr<Environment>, Error> AssetLoader::load_environment(
        const AssetHandle handle) {
        return load_runtime_asset<Environment>(m_database, m_registry, handle,
            AssetType::Environment,
            [this](const AssetRecord& record) -> Result<std::shared_ptr<Environment>, Error> {
                if(m_database.is_read_only()) {
                    auto data = read_environment_data(record);
                    if(!data)
                        return Result<std::shared_ptr<Environment>, Error>::failure(data.error());
                    return m_render_factory.prepare(std::move(data).value());
                }
                auto prepared = m_imports.prepare_environment(record, m_environment_bytes);
                if(!prepared)
                    return Result<std::shared_ptr<Environment>, Error>::failure({prepared.error()});
                return m_render_factory.prepare(std::move(prepared).value().data);
            });
    }

    Result<std::shared_ptr<Irradiance>, Error> AssetLoader::load_irradiance(
        const AssetHandle handle) {
        using Loaded = Result<std::shared_ptr<Irradiance>, Error>;
        return load_runtime_asset<Irradiance>(m_database, m_registry, handle, AssetType::Irradiance,
            [this](const AssetRecord& record) -> Loaded {
                auto path = m_database.paths().resolve_asset_path(record.path);
                if(!path)
                    return Loaded::failure({path.error()});
                auto data = IrradianceData::load(path.value());
                if(!data)
                    return Loaded::failure({data.error()});
                return m_render_factory.prepare(std::move(data).value());
            });
    }

    Result<std::shared_ptr<Texture>, Error> AssetLoader::prepare_texture(
        const AssetRecord& record, const TextureImportSettings& import_settings) {
        auto data = m_imports.prepare_texture(record, import_settings, m_texture_bytes);
        if(!data)
            return Result<std::shared_ptr<Texture>, Error>::failure({data.error()});
        return m_render_factory.prepare(std::move(data).value());
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

        return RenderAssetFactory::prepare_material(record.path.stem().string(), data, textures);
    }
}
