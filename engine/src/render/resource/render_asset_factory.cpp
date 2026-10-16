#include "asset/runtime/render_asset_factory.h"

#include "asset/data/material_data.h"
#include "asset/data/environment_data.h"
#include "asset/data/irradiance_data.h"
#include "asset/data/mesh_data.h"
#include "render/material/material.h"
#include "render/resource/environment.h"
#include "render/resource/irradiance.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_factory.h"
#include "render/resource/texture.h"

#include <utility>
#include <unordered_set>

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

    Result<std::shared_ptr<Irradiance>, Error> RenderAssetFactory::prepare(IrradianceData data) {
        return asset_creation<Irradiance>(Irradiance::try_create(m_resources, std::move(data)));
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
        if(!material->set_surface(data.surface))
            return Preparation::failure({"Invalid material surface"});
        for(const auto& [property, handle] : data.texture_properties) {
            const auto texture = textures.find(property);
            if(!handle || texture == textures.end() || !texture->second)
                return Preparation::failure({"Material texture '" + property + "' is unresolved"});
            material->set_texture_property(property, texture->second, handle);
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

    RenderAssetFactory::ResidentBytes RenderAssetFactory::resident_bytes(
        const ResidencyInput& input) {
        ResidentBytes result;
        std::unordered_set<const Mesh*> meshes;
        std::unordered_set<const Texture*> textures;
        const auto count_texture = [&](const std::shared_ptr<Texture>& texture) {
            if(texture && textures.insert(texture.get()).second)
                result.texture_pixel_bytes += texture->pixel_bytes();
        };
        for(const auto& mesh : input.meshes)
            if(mesh && meshes.insert(mesh.get()).second)
                result.mesh_buffer_bytes += mesh->buffer_bytes();
        for(const auto& texture : input.textures)
            count_texture(texture);
        for(const auto& environment : input.environments)
            if(environment) {
                count_texture(environment->background);
                count_texture(environment->irradiance);
                count_texture(environment->specular);
                count_texture(environment->brdf);
            }
        for(const auto& irradiance : input.irradiance)
            if(irradiance)
                count_texture(irradiance->probes);
        return result;
    }

}
