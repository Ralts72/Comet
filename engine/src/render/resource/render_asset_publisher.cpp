#include "asset/runtime/render_asset_publisher.h"

#include "asset/data/material_data.h"
#include "asset/registry.h"
#include "render/material/material.h"
#include "render/resource/environment.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_factory.h"
#include "render/resource/texture.h"

#include <utility>

namespace Comet {
    namespace {
        template<typename T> Result<std::shared_ptr<T>, Error> asset_creation(auto attempt) {
            if(!attempt)
                return Result<std::shared_ptr<T>, Error>::failure(attempt.error().as_error());
            return Result<std::shared_ptr<T>, Error>::success(std::move(attempt).value());
        }

        template<typename T>
        bool publish_asset(AssetRegistry& registry, AssetHandle handle,
            const std::shared_ptr<T>& asset, bool replace) {
            if(replace)
                return registry.replace_asset(handle, asset);
            return registry.register_asset(handle, asset);
        }
    }

    RenderAssetPublisher::RenderAssetPublisher(
        AssetRegistry& registry, RenderResourceFactory& resources)
        : m_registry(registry), m_resources(resources) {}

    Result<std::shared_ptr<Mesh>, Error> RenderAssetPublisher::prepare(const MeshData& data) {
        return asset_creation<Mesh>(m_resources.try_create_mesh(data));
    }

    Result<std::shared_ptr<Texture>, Error> RenderAssetPublisher::prepare(const TextureData& data) {
        return asset_creation<Texture>(m_resources.try_create_texture(data));
    }

    Result<std::shared_ptr<Environment>, Error> RenderAssetPublisher::prepare(
        const EnvironmentData& data) {
        return asset_creation<Environment>(Environment::try_create(m_resources, data));
    }

    Result<std::shared_ptr<Environment>, Error> RenderAssetPublisher::prepare_preview(
        const TextureData& background) {
        auto texture = prepare(background);
        if(!texture)
            return Result<std::shared_ptr<Environment>, Error>::failure(texture.error());
        auto environment = std::make_shared<Environment>();
        environment->background = std::move(texture).value();
        return Result<std::shared_ptr<Environment>, Error>::success(std::move(environment));
    }

    Result<std::shared_ptr<Material>, Error> RenderAssetPublisher::prepare_material(
        std::string name, const MaterialData& data,
        const std::map<std::string, std::shared_ptr<Texture>>& textures) {
        using Preparation = Result<std::shared_ptr<Material>, Error>;
        auto material =
            std::make_shared<Material>(std::move(name), data.template_name, data.shader_program);
        for(const auto& [property, handle] : data.texture_properties) {
            const auto texture = textures.find(property);
            if(!handle || texture == textures.end() || !texture->second)
                return Preparation::failure({"Material texture '" + property + "' is unresolved"});
            material->set_texture_property(property, texture->second);
        }
        for(const auto& [property, value] : data.scalar_properties) {
            if(!material->set_scalar_property(property, value))
                return Preparation::failure({"Material scalar '" + property + "' must be finite"});
        }
        for(const auto& [property, value] : data.vector_properties) {
            if(!material->set_vector_property(property, value))
                return Preparation::failure({"Material vector '" + property + "' must be finite"});
        }
        return Preparation::success(std::move(material));
    }

    std::shared_ptr<Mesh> RenderAssetPublisher::mesh(AssetHandle handle) const {
        return m_registry.resolve<Mesh>(handle);
    }

    std::shared_ptr<Texture> RenderAssetPublisher::texture(AssetHandle handle) const {
        return m_registry.resolve<Texture>(handle);
    }

    std::shared_ptr<Material> RenderAssetPublisher::material(AssetHandle handle) const {
        return m_registry.resolve<Material>(handle);
    }

    std::shared_ptr<Environment> RenderAssetPublisher::environment(AssetHandle handle) const {
        return m_registry.resolve<Environment>(handle);
    }

    bool RenderAssetPublisher::has_lighting(const std::shared_ptr<Environment>& environment) const {
        return environment && environment->has_lighting();
    }

    bool RenderAssetPublisher::publish(
        AssetHandle handle, const std::shared_ptr<Mesh>& asset, bool replace) {
        return publish_asset(m_registry, handle, asset, replace);
    }

    bool RenderAssetPublisher::publish(
        AssetHandle handle, const std::shared_ptr<Texture>& asset, bool replace) {
        return publish_asset(m_registry, handle, asset, replace);
    }

    bool RenderAssetPublisher::publish(
        AssetHandle handle, const std::shared_ptr<Material>& asset, bool replace) {
        return publish_asset(m_registry, handle, asset, replace);
    }

    bool RenderAssetPublisher::publish(
        AssetHandle handle, const std::shared_ptr<Environment>& asset, bool replace) {
        return publish_asset(m_registry, handle, asset, replace);
    }
}
