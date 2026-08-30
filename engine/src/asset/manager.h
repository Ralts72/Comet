#pragma once

#include "asset/database.h"
#include "common/export.h"
#include "core/project_paths.h"

#include <memory>

namespace Comet {
    class AssetRegistry;
    class Material;
    class ResourceManager;
    class Texture;

    class COMET_API AssetManager final {
    public:
        AssetManager(
            ProjectPaths paths,
            AssetRegistry& registry,
            ResourceManager& resource_manager);

        [[nodiscard]] AssetScanReport scan();
        [[nodiscard]] std::shared_ptr<Texture> load_texture(AssetHandle handle);
        [[nodiscard]] std::shared_ptr<Material> load_material(AssetHandle handle);

        [[nodiscard]] const AssetDatabase& get_database() const noexcept {
            return m_database;
        }

    private:
        ProjectPaths m_paths;
        AssetDatabase m_database;
        AssetRegistry& m_registry;
        ResourceManager& m_resource_manager;
    };
}
