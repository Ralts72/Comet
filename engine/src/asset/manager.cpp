#include "asset/manager.h"

#include "asset/cache/mesh_import_cache.h"
#include "asset/import/mesh_importer.h"
#include "asset/import/texture_importer.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "common/file_io.h"
#include "diagnostics/logger.h"
#include "render/material.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_factory.h"
#include "render/resource/texture.h"

#include <exception>
#include <map>
#include <queue>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Comet {
    AssetManager::AssetManager(
        ProjectPaths paths,
        AssetRegistry& registry,
        RenderResourceFactory& resource_factory)
        : m_paths(std::move(paths)),
          m_database(m_paths),
          m_registry(registry),
          m_resource_factory(resource_factory) {}

    AssetScanReport AssetManager::scan() {
        AssetScanReport report = m_database.scan();
        if(!report.snapshot_updated) {
            return report;
        }

        std::unordered_set<AssetHandle> invalidated(
            report.removed_assets.begin(),
            report.removed_assets.end());
        std::queue<AssetHandle> pending_invalidations;
        for(const AssetHandle handle: report.removed_assets) {
            pending_invalidations.push(handle);
        }
        while(!pending_invalidations.empty()) {
            const AssetHandle dependency = pending_invalidations.front();
            pending_invalidations.pop();
            for(const AssetHandle dependent:
                m_database.get_dependents(dependency)) {
                if(invalidated.insert(dependent).second) {
                    pending_invalidations.push(dependent);
                }
            }
        }
        for(const AssetHandle handle: invalidated) {
            static_cast<void>(m_registry.unregister_asset(handle));
        }

        for(const AssetHandle handle: report.modified_assets) {
            if(invalidated.contains(handle) || !m_registry.contains(handle)) {
                continue;
            }

            const AssetRecord* record = m_database.find(handle);
            if(!record) {
                continue;
            }
            switch(record->type) {
                case AssetType::Texture:
                    if(!refresh_loaded_texture(*record)) {
                        LOG_ERROR(
                            "Failed to refresh modified texture asset handle {}",
                            handle.value());
                    }
                    break;
                case AssetType::Material:
                    if(!reload_material(handle)) {
                        LOG_ERROR(
                            "Failed to refresh modified material asset handle {}",
                            handle.value());
                    }
                    break;
                case AssetType::Mesh:
                    if(!refresh_loaded_mesh(*record)) {
                        LOG_ERROR(
                            "Failed to refresh modified mesh asset handle {}",
                            handle.value());
                    }
                    break;
                default:
                    static_cast<void>(m_registry.unregister_asset(handle));
                    LOG_WARN(
                        "Unloaded modified asset handle {} because runtime reload is not implemented for type '{}'",
                        handle.value(),
                        to_string(record->type));
                    break;
            }
        }
        return report;
    }

    std::shared_ptr<Mesh> AssetManager::load_mesh(
        const AssetHandle handle) {
        if(!handle) {
            LOG_ERROR("Cannot load a mesh with an invalid asset handle");
            return nullptr;
        }

        if(const auto mesh = m_registry.resolve<Mesh>(handle)) {
            return mesh;
        }
        if(m_registry.contains(handle)) {
            LOG_ERROR(
                "Asset handle {} is already registered with another runtime type",
                handle.value());
            return nullptr;
        }

        const AssetRecord* record = m_database.find(handle);
        if(!record) {
            LOG_ERROR("Mesh asset handle {} is not indexed", handle.value());
            return nullptr;
        }
        if(record->type != AssetType::Mesh) {
            LOG_ERROR(
                "Asset handle {} has type '{}', expected 'mesh'",
                handle.value(),
                to_string(record->type));
            return nullptr;
        }

        auto mesh = create_runtime_mesh(*record);
        if(!mesh) {
            return nullptr;
        }
        if(!m_registry.register_asset(handle, mesh)) {
            LOG_ERROR(
                "Failed to register runtime mesh for asset handle {}",
                handle.value());
            return nullptr;
        }
        return mesh;
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
        std::string serialized_data;
        try {
            serialized_data = serializer.serialize(data);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        auto material = create_runtime_material(*record, data);
        if(!material) {
            return nullptr;
        }

        try {
            write_text_file_atomic(
                m_paths.assets() / record->path,
                serialized_data);
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

    std::shared_ptr<Mesh> AssetManager::create_runtime_mesh(
        const AssetRecord& record) {
        const std::filesystem::path asset_root = m_paths.assets();
        const std::filesystem::path source_path = asset_root / record.path;
        const std::filesystem::path cache_path =
            m_paths.cache() / "imported" / "mesh"
            / (std::to_string(record.handle.value()) + ".bin");
        MeshData data;
        try {
            if(auto cached = MeshImportCache::load_if_current(
                   cache_path,
                   asset_root,
                   source_path,
                   MeshImporter::OUTPUT_VERSION)) {
                data = std::move(*cached);
                LOG_DEBUG(
                    "Loaded mesh import cache '{}' (handle {})",
                    record.path.generic_string(),
                    record.handle.value());
            } else {
                MeshImportResult imported =
                    MeshImporter{}.import_with_dependencies(source_path);
                data = std::move(imported.data);
                try {
                    MeshImportCache::store(
                        cache_path,
                        asset_root,
                        source_path,
                        imported.source_dependencies,
                        MeshImporter::OUTPUT_VERSION,
                        data);
                } catch(const std::exception& exception) {
                    LOG_WARN(
                        "Imported mesh '{}', but could not update its import cache: {}",
                        record.path.generic_string(),
                        exception.what());
                }
            }
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }
        return m_resource_factory.create_mesh(data);
    }

    bool AssetManager::refresh_loaded_mesh(const AssetRecord& record) {
        const auto previous_mesh = m_registry.resolve<Mesh>(record.handle);
        if(!previous_mesh) {
            return !m_registry.contains(record.handle);
        }

        auto mesh = create_runtime_mesh(record);
        if(!mesh || !m_registry.replace_asset(record.handle, mesh)) {
            return false;
        }

        LOG_INFO(
            "Reloaded mesh asset '{}' (handle {})",
            record.path.generic_string(),
            record.handle.value());
        return true;
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
        return m_resource_factory.create_texture(data);
    }

    bool AssetManager::refresh_loaded_texture(const AssetRecord& record) {
        const auto previous_texture = m_registry.resolve<Texture>(record.handle);
        if(!previous_texture) {
            return !m_registry.contains(record.handle);
        }

        const auto* settings = std::get_if<TextureImportSettings>(
            &record.import_settings);
        if(!settings) {
            LOG_ERROR(
                "Texture asset handle {} has incompatible import settings",
                record.handle.value());
            return false;
        }

        auto texture = create_runtime_texture(record, *settings);
        if(!texture
           || !m_registry.replace_asset(record.handle, texture)) {
            return false;
        }

        for(const AssetHandle dependent:
            m_database.get_dependents(record.handle)) {
            const AssetRecord* dependent_record = m_database.find(dependent);
            if(dependent_record
               && dependent_record->type == AssetType::Material
               && m_registry.resolve<Material>(dependent)
               && !reload_material(dependent)) {
                LOG_ERROR(
                    "Texture handle {} was refreshed, but dependent material handle {} could not be refreshed",
                    record.handle.value(),
                    dependent.value());
            }
        }
        LOG_INFO(
            "Reloaded texture asset '{}' (handle {})",
            record.path.generic_string(),
            record.handle.value());
        return true;
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
            data.template_name);
        for(const auto& [property_name, texture]: textures) {
            material->set_texture_property(property_name, texture);
        }
        return material;
    }
}
