#include "asset/runtime/render_asset_factory.h"

#include "asset/data/material_data.h"
#include "asset/data/environment_data.h"
#include "asset/data/mesh_data.h"
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

    }

    RenderAssetFactory::RenderAssetFactory(RenderResourceFactory& resources)
        : m_resources(resources) {}

    Result<std::shared_ptr<Mesh>, Error> RenderAssetFactory::prepare(MeshData data) {
        return asset_creation<Mesh>(m_resources.try_create_mesh(std::move(data)));
    }

    Result<std::shared_ptr<Texture>, Error> RenderAssetFactory::prepare(TextureData data) {
        return asset_creation<Texture>(m_resources.try_create_texture(std::move(data)));
    }

    Result<std::shared_ptr<Environment>, Error> RenderAssetFactory::prepare(EnvironmentData data) {
        return asset_creation<Environment>(Environment::try_create(m_resources, std::move(data)));
    }

    Result<std::shared_ptr<Environment>, Error> RenderAssetFactory::prepare_preview(
        TextureData background) {
        auto texture = prepare(std::move(background));
        if(!texture)
            return Result<std::shared_ptr<Environment>, Error>::failure(texture.error());
        auto environment = std::make_shared<Environment>();
        environment->background = std::move(texture).value();
        return Result<std::shared_ptr<Environment>, Error>::success(std::move(environment));
    }

    Result<std::shared_ptr<Material>, Error> RenderAssetFactory::prepare_material(std::string name,
        const MaterialData& data, const std::map<std::string, std::shared_ptr<Texture>>& textures) {
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

    bool RenderAssetFactory::has_lighting(const std::shared_ptr<Environment>& environment) {
        return environment && environment->has_lighting();
    }

}
