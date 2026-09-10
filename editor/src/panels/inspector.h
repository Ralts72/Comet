#pragma once

#include "asset/database.h"
#include "asset/material_data.h"
#include "editor_panel.h"
#include "command_history.h"
#include "editor_state.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace Comet {
    class ComponentRegistry;
    class Entity;
}

namespace CometEditor {
    class PropertyEditorRegistry;
    class SelectionService;

    class InspectorPanel: public EditorPanel {
    public:
        using UpdateMaterialCallback =
            std::function<bool(Comet::AssetHandle, const Comet::MaterialData&)>;
        using ReimportTextureCallback =
            std::function<bool(Comet::AssetHandle, Comet::TextureImportSettings)>;

        using PrepareAsset = std::function<bool(Comet::AssetHandle, Comet::AssetType)>;

        InspectorPanel(const EditorState& state, SelectionService& selection,
            CommandHistory& history, PropertyEditTransaction& property_edit,
            const Comet::ComponentRegistry& component_registry,
            const PropertyEditorRegistry& property_editor_registry,
            const Comet::AssetDatabase& asset_database, std::filesystem::path assets_root,
            UpdateMaterialCallback update_material_callback,
            ReimportTextureCallback reimport_texture_callback,
            PrepareAsset prepare_asset = {});

        void render() override;
        void invalidate_asset_cache();

    private:
        void render_entity(Comet::Entity entity);
        void render_property(Comet::Entity entity,
            const Comet::ComponentDescriptor& component,
            const Comet::PropertyDescriptor& property);
        void render_asset(Comet::AssetHandle handle);
        void render_texture(const Comet::AssetRecord& record);
        void render_material(const Comet::AssetRecord& record);
        void load_asset(const Comet::AssetRecord& record);
        void reimport_texture(const Comet::AssetRecord& record,
            const Comet::TextureImportSettings& previous_settings);
        void update_material(
            const Comet::AssetRecord& record, const Comet::MaterialData& previous_data);
        [[nodiscard]] std::string validate_material() const;

        const EditorState& m_state;
        PrepareAsset m_prepare_asset;
        SelectionService& m_selection;
        CommandHistory& m_history;
        PropertyEditTransaction& m_property_edit;
        const Comet::ComponentRegistry& m_component_registry;
        const PropertyEditorRegistry& m_property_editor_registry;
        const Comet::AssetDatabase& m_asset_database;
        std::filesystem::path m_assets_root;
        UpdateMaterialCallback m_update_material_callback;
        ReimportTextureCallback m_reimport_texture_callback;
        Comet::AssetHandle m_loaded_asset;
        std::optional<Comet::TextureImportSettings> m_texture_import_settings;
        std::optional<Comet::MaterialData> m_material_data;
        std::string m_asset_error;
    };

}
