#pragma once

#include "asset/database.h"
#include "asset/material_data.h"
#include "editor.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Comet {
    class ComponentRegistry;
    class Entity;
}

namespace CometEditor {
    class PropertyEditorRegistry;
    class SelectionService;

    class InspectorPanel : public EditorPanel {
    public:
        using UpdateMaterialCallback = std::function<bool(
            Comet::AssetHandle,
            const Comet::MaterialData&)>;
        using ReimportTextureCallback = std::function<bool(
            Comet::AssetHandle,
            Comet::TextureImportSettings)>;

        InspectorPanel(
            SelectionService& selection,
            const Comet::ComponentRegistry& component_registry,
            const PropertyEditorRegistry& property_editor_registry,
            const Comet::AssetDatabase& asset_database,
            std::filesystem::path assets_root,
            UpdateMaterialCallback update_material_callback,
            ReimportTextureCallback reimport_texture_callback);

        void render() override;
        void invalidate_asset_cache();

    private:
        void render_entity(Comet::Entity entity) const;
        void render_asset(Comet::AssetHandle handle);
        void render_texture(const Comet::AssetRecord& record);
        void render_material(const Comet::AssetRecord& record);
        void load_asset(const Comet::AssetRecord& record);
        void reimport_texture(
            const Comet::AssetRecord& record,
            const Comet::TextureImportSettings& previous_settings);
        void update_material(
            const Comet::AssetRecord& record,
            const Comet::MaterialData& previous_data);
        [[nodiscard]] std::string validate_material() const;

        SelectionService& m_selection;
        const Comet::ComponentRegistry& m_component_registry;
        const PropertyEditorRegistry& m_property_editor_registry;
        const Comet::AssetDatabase& m_asset_database;
        std::filesystem::path m_assets_root;
        UpdateMaterialCallback m_update_material_callback;
        ReimportTextureCallback m_reimport_texture_callback;
        Comet::AssetHandle m_loaded_asset;
        std::optional<Comet::TextureImportSettings> m_texture_import_settings;
        std::optional<Comet::MaterialData> m_material_data;
        std::vector<Comet::AssetRecord> m_texture_assets;
        std::string m_asset_error;
    };

}
