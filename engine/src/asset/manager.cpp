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
#include <vector>

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

        auto texture = create_runtime_texture(*record, *settings);
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

    std::shared_ptr<Texture> AssetManager::reimport_texture(
        const AssetHandle handle,
        TextureImportSettings import_settings) {
        if(!handle) {
            LOG_ERROR("Cannot reimport a texture with an invalid asset handle");
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

        const auto previous_texture = m_registry.resolve<Texture>(handle);
        if(m_registry.contains(handle) && !previous_texture) {
            LOG_ERROR(
                "Asset handle {} is already registered with another runtime type",
                handle.value());
            return nullptr;
        }

        std::vector<AssetHandle> dependent_materials;
        for(const AssetHandle dependent: m_database.get_dependents(handle)) {
            const AssetRecord* dependent_record = m_database.find(dependent);
            if(dependent_record
               && dependent_record->type == AssetType::Material
               && m_registry.resolve<Material>(dependent)) {
                dependent_materials.push_back(dependent);
            }
        }

        auto texture = create_runtime_texture(*record, import_settings);
        if(!texture) {
            return nullptr;
        }

        try {
            m_database.update_import_settings(handle, import_settings);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        const bool published = previous_texture
            ? m_registry.replace_asset(handle, texture)
            : m_registry.register_asset(handle, texture);
        if(!published) {
            LOG_ERROR(
                "Failed to publish reimported texture for asset handle {}",
                handle.value());
            return nullptr;
        }

        for(const AssetHandle material_handle: dependent_materials) {
            if(!reload_material(material_handle)) {
                LOG_ERROR(
                    "Texture handle {} was reimported, but dependent material handle {} could not be refreshed",
                    handle.value(),
                    material_handle.value());
            }
        }
        LOG_INFO(
            "Reimported texture asset '{}' (handle {}, color_space={}, flip_y={})",
            record->path.generic_string(),
            handle.value(),
            to_string(import_settings.color_space),
            import_settings.flip_y);
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

        MaterialData data;
        try {
            data = MaterialSerializer{}.load(m_paths.assets() / record->path);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        auto material = create_runtime_material(*record, data);
        if(!material) {
            return nullptr;
        }

        try {
            m_database.update_dependencies(
                handle,
                get_asset_dependencies(data));
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
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
        LOG_INFO(
            "Reloaded material asset '{}' (handle {})",
            record->path.generic_string(),
            handle.value());
        return material;
    }

    std::shared_ptr<Material> AssetManager::update_material(
        const AssetHandle handle,
        const MaterialData& data) {
        if(!handle) {
            LOG_ERROR("Cannot update a material with an invalid asset handle");
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

        const MaterialSerializer serializer;
        try {
            static_cast<void>(serializer.serialize(data));
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        auto material = create_runtime_material(*record, data);
        if(!material) {
            return nullptr;
        }

        try {
            serializer.save(data, m_paths.assets() / record->path);
            m_database.update_dependencies(
                handle,
                get_asset_dependencies(data));
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        const bool published = has_runtime_asset
            ? m_registry.replace_asset(handle, material)
            : m_registry.register_asset(handle, material);
        if(!published) {
            LOG_ERROR(
                "Failed to publish updated material for asset handle {}",
                handle.value());
            return nullptr;
        }
        LOG_INFO(
            "Updated material asset '{}' (handle {})",
            record->path.generic_string(),
            handle.value());
        return material;
    }

    std::shared_ptr<Texture> AssetManager::create_runtime_texture(
        const AssetRecord& record,
        const TextureImportSettings& import_settings) {
        TextureData data;
        try {
            data = TextureImporter{}.import(
                m_paths.assets() / record.path,
                import_settings);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }
        return m_resource_manager.create_texture(data);
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

        return create_runtime_material(record, data);
    }

    std::shared_ptr<Material> AssetManager::create_runtime_material(
        const AssetRecord& record,
        const MaterialData& data) {
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
