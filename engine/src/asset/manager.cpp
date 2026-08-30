#include "asset/manager.h"

#include "asset/import/texture_importer.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "common/logger.h"
#include "render/material.h"
#include "render/resource_manager.h"
#include "render/texture.h"

#include <exception>
#include <map>
#include <string>
#include <utility>

namespace Comet {
    AssetManager::AssetManager(
        ProjectPaths paths,
        AssetRegistry& registry,
        ResourceManager& resource_manager)
        : m_paths(std::move(paths)),
          m_database(m_paths),
          m_registry(registry),
          m_resource_manager(resource_manager) {}

    AssetScanReport AssetManager::scan() {
        return m_database.scan();
    }

    std::shared_ptr<Texture> AssetManager::load_texture(
        const AssetHandle handle) {
        if(!handle) {
            LOG_ERROR("Cannot load a texture with an invalid asset handle");
            return nullptr;
        }

        if(const auto texture = m_registry.resolve<Texture>(handle)) {
            return texture;
        }
        if(m_registry.contains(handle)) {
            LOG_ERROR(
                "Asset handle {} is already registered with another runtime type",
                handle.value());
            return nullptr;
        }

        const AssetRecord* record = m_database.find(handle);
        if(!record) {
            LOG_ERROR("Texture asset handle {} is not indexed", handle.value());
            return nullptr;
        }
        if(record->type != AssetType::Texture) {
            LOG_ERROR(
                "Asset handle {} has type '{}', expected 'texture'",
                handle.value(),
                to_string(record->type));
            return nullptr;
        }

        const auto* settings = std::get_if<TextureImportSettings>(
            &record->import_settings);
        if(!settings) {
            LOG_ERROR(
                "Texture asset handle {} has incompatible import settings",
                handle.value());
            return nullptr;
        }

        TextureData data;
        try {
            data = TextureImporter{}.import(
                m_paths.assets() / record->path,
                *settings);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        auto texture = m_resource_manager.create_texture(data);
        if(!texture) {
            return nullptr;
        }
        if(!m_registry.register_asset(handle, texture)) {
            LOG_ERROR(
                "Failed to register runtime texture for asset handle {}",
                handle.value());
            return nullptr;
        }
        return texture;
    }

    std::shared_ptr<Material> AssetManager::load_material(
        const AssetHandle handle) {
        if(!handle) {
            LOG_ERROR("Cannot load a material with an invalid asset handle");
            return nullptr;
        }

        if(const auto material = m_registry.resolve<Material>(handle)) {
            return material;
        }
        if(m_registry.contains(handle)) {
            LOG_ERROR(
                "Asset handle {} is already registered with another runtime type",
                handle.value());
            return nullptr;
        }

        const AssetRecord* record = m_database.find(handle);
        if(!record) {
            LOG_ERROR("Material asset handle {} is not indexed", handle.value());
            return nullptr;
        }
        if(record->type != AssetType::Material) {
            LOG_ERROR(
                "Asset handle {} has type '{}', expected 'material'",
                handle.value(),
                to_string(record->type));
            return nullptr;
        }

        auto material = create_runtime_material(*record);
        if(!material) {
            return nullptr;
        }
        if(!m_registry.register_asset(handle, material)) {
            LOG_ERROR(
                "Failed to register runtime material for asset handle {}",
                handle.value());
            return nullptr;
        }
        return material;
    }

    std::shared_ptr<Material> AssetManager::reload_material(
        const AssetHandle handle) {
        if(!handle) {
            LOG_ERROR("Cannot reload a material with an invalid asset handle");
            return nullptr;
        }

        const AssetRecord* record = m_database.find(handle);
        if(!record) {
            LOG_ERROR("Material asset handle {} is not indexed", handle.value());
            return nullptr;
        }
        if(record->type != AssetType::Material) {
            LOG_ERROR(
                "Asset handle {} has type '{}', expected 'material'",
                handle.value(),
                to_string(record->type));
            return nullptr;
        }

        const bool has_runtime_asset = m_registry.contains(handle);
        if(has_runtime_asset && !m_registry.resolve<Material>(handle)) {
            LOG_ERROR(
                "Asset handle {} is already registered with another runtime type",
                handle.value());
            return nullptr;
        }

        auto material = create_runtime_material(*record);
        if(!material) {
            return nullptr;
        }

        const bool published = has_runtime_asset
            ? m_registry.replace_asset(handle, material)
            : m_registry.register_asset(handle, material);
        if(!published) {
            LOG_ERROR(
                "Failed to publish reloaded material for asset handle {}",
                handle.value());
            return nullptr;
        }
        return material;
    }

    std::shared_ptr<Material> AssetManager::create_runtime_material(
        const AssetRecord& record) {
        MaterialData data;
        try {
            data = MaterialSerializer{}.load(m_paths.assets() / record.path);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        std::map<std::string, std::shared_ptr<Texture>> textures;
        for(const auto& [property_name, texture_handle]: data.texture_properties) {
            auto texture = load_texture(texture_handle);
            if(!texture) {
                LOG_ERROR(
                    "Failed to resolve texture handle {} for material '{}' property '{}'",
                    texture_handle.value(),
                    record.path.generic_string(),
                    property_name);
                return nullptr;
            }
            textures.emplace(property_name, std::move(texture));
        }

        auto material = std::make_shared<Material>(
            record.path.stem().string(),
            data.template_name,
            MaterialConfig{});
        for(const auto& [property_name, texture]: textures) {
            material->set_property_texture(property_name, texture);
        }
        return material;
    }
}
