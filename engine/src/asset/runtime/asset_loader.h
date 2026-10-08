#pragma once

#include "asset/database.h"
#include "common/error.h"
#include "common/result.h"

#include <memory>

namespace Comet {
    class AssetRegistry;
    class ImportService;
    class RenderAssetPublisher;
    class Mesh;
    class Texture;
    class Material;
    class ShaderProgramArtifact;
    class AudioClip;
    struct Environment;
    struct MaterialData;

    // CPU reads and dependency loading; uses the same index and Registry as AssetManager.
    class AssetLoader final {
    public:
        AssetLoader(AssetDatabase& database, AssetRegistry& registry, ImportService& imports,
            RenderAssetPublisher& render_assets, std::size_t mesh_bytes, std::size_t texture_bytes,
            std::size_t environment_bytes);

        Result<std::shared_ptr<Mesh>, Error> load_mesh(AssetHandle handle);
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
        AssetDatabase& m_database;
        AssetRegistry& m_registry;
        ImportService& m_imports;
        RenderAssetPublisher& m_render_assets;
        std::size_t m_mesh_bytes;
        std::size_t m_texture_bytes;
        std::size_t m_environment_bytes;
    };
}
