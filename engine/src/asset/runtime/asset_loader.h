#pragma once

#include "animation/animation_controller.h"
#include "asset/handle.h"
#include "asset/data/mesh_data.h"
#include "common/error.h"
#include "common/export.h"
#include "common/result.h"

#include <cstddef>
#include <memory>
#include <filesystem>
#include <unordered_map>

namespace Comet {
    class AssetDatabase;
    class COMET_API Prefab;
    class ComponentRegistry;
    class ModelArtifact;
    struct ModelData;
    class AssetRegistry;
    class ImportService;
    class RenderAssetFactory;
    class COMET_API Mesh;
    class COMET_API Texture;
    class COMET_API Material;
    class ShaderProgramArtifact;
    class AudioClip;
    struct Environment;
    struct MaterialData;
    struct AssetRecord;
    struct TextureImportSettings;

    // CPU reads and dependency loading; uses the same index and Registry as AssetManager.
    class AssetLoader final {
    public:
        AssetLoader(AssetDatabase& database, AssetRegistry& registry, ImportService& imports,
            RenderAssetFactory& render_factory, std::size_t mesh_bytes, std::size_t texture_bytes,
            std::size_t environment_bytes);

        [[nodiscard]] Result<std::shared_ptr<AnimationController>, Error> load_animation_controller(
            AssetHandle handle);
        Result<std::shared_ptr<Prefab>, Error> load_prefab(
            AssetHandle handle, const ComponentRegistry& components);
        Result<std::shared_ptr<const ModelData>, Error> load_model(AssetHandle handle);
        void invalidate_model(AssetHandle handle);
        bool model_is_loaded(AssetHandle handle) const;
        Result<std::shared_ptr<Mesh>, Error> load_mesh(AssetHandle handle);
        Result<MeshData, Error> read_mesh_data(AssetHandle handle);
        Result<std::shared_ptr<Texture>, Error> load_texture(AssetHandle handle);
        Result<std::shared_ptr<Material>, Error> load_material(AssetHandle handle);
        Result<std::shared_ptr<Environment>, Error> load_environment(AssetHandle handle);
        Result<std::shared_ptr<ShaderProgramArtifact>, Error> load_shader_program(
            AssetHandle handle);
        Result<std::shared_ptr<AudioClip>, Error> load_audio(AssetHandle handle);

        Result<std::shared_ptr<Texture>, Error> prepare_texture(
            const AssetRecord& record, const TextureImportSettings& settings);
        Result<std::shared_ptr<Material>, Error> prepare_material(
            const AssetRecord& record, const MaterialData& data);

    private:
        Result<MeshData, Error> read_mesh_data(const AssetRecord& record);
        Result<std::shared_ptr<const ModelArtifact>, Error> model_artifact(AssetHandle handle);
        Result<std::filesystem::path, Error> product_path(const AssetRecord& record);
        struct CachedModel {
            AssetRevision revision;
            std::shared_ptr<const ModelArtifact> artifact;
            std::unordered_map<AssetHandle, std::size_t> children;
        };
        std::unordered_map<AssetHandle, CachedModel> m_models;
        AssetDatabase& m_database;
        AssetRegistry& m_registry;
        ImportService& m_imports;
        RenderAssetFactory& m_render_factory;
        std::size_t m_mesh_bytes;
        std::size_t m_texture_bytes;
        std::size_t m_environment_bytes;
    };
}
