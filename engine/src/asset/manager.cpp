#include "asset/manager.h"

#include "asset/registry.h"
#include "asset/texture_importer.h"
#include "common/logger.h"
#include "render/resource_manager.h"
#include "render/texture.h"

#include <exception>
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

        TextureData data;
        try {
            data = TextureImporter{}.import(m_paths.assets() / record->path);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        auto texture = m_resource_manager.create_texture(handle, data);
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
}
