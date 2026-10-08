#pragma once

#include "asset/handle.h"
#include "common/error.h"
#include "common/export.h"
#include "common/result.h"

#include <map>
#include <memory>
#include <string>

namespace Comet {
    class AssetRegistry;
    class RenderResourceFactory;
    class Mesh;
    class Texture;
    class Material;
    struct Environment;
    struct MeshData;
    struct TextureData;
    struct EnvironmentData;
    struct MaterialData;

    // Render implements this boundary without reading source files or borrowing the asset index.
    // Preparation never mutates Registry; the asset owner validates revisions before publication.
    class COMET_API RenderAssetPublisher final {
    public:
        RenderAssetPublisher(AssetRegistry& registry, RenderResourceFactory& resources);

        [[nodiscard]] Result<std::shared_ptr<Mesh>, Error> prepare(const MeshData& data);
        [[nodiscard]] Result<std::shared_ptr<Texture>, Error> prepare(const TextureData& data);
        [[nodiscard]] Result<std::shared_ptr<Environment>, Error> prepare(
            const EnvironmentData& data);
        [[nodiscard]] Result<std::shared_ptr<Environment>, Error> prepare_preview(
            const TextureData& background);
        [[nodiscard]] Result<std::shared_ptr<Material>, Error> prepare_material(std::string name,
            const MaterialData& data,
            const std::map<std::string, std::shared_ptr<Texture>>& textures);

        [[nodiscard]] std::shared_ptr<Mesh> mesh(AssetHandle handle) const;
        [[nodiscard]] std::shared_ptr<Texture> texture(AssetHandle handle) const;
        [[nodiscard]] std::shared_ptr<Material> material(AssetHandle handle) const;
        [[nodiscard]] std::shared_ptr<Environment> environment(AssetHandle handle) const;
        [[nodiscard]] bool has_lighting(const std::shared_ptr<Environment>& environment) const;

        [[nodiscard]] bool publish(
            AssetHandle handle, const std::shared_ptr<Mesh>& asset, bool replace);
        [[nodiscard]] bool publish(
            AssetHandle handle, const std::shared_ptr<Texture>& asset, bool replace);
        [[nodiscard]] bool publish(
            AssetHandle handle, const std::shared_ptr<Material>& asset, bool replace);
        [[nodiscard]] bool publish(
            AssetHandle handle, const std::shared_ptr<Environment>& asset, bool replace);

    private:
        AssetRegistry& m_registry;
        RenderResourceFactory& m_resources;
    };
}
