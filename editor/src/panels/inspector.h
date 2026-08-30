#pragma once

#include "asset/database.h"
#include "asset/material_data.h"
#include "editor.h"

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

    class InspectorPanel : public EditorPanel {
    public:
        using ReloadMaterialCallback = std::function<bool(Comet::AssetHandle)>;

        InspectorPanel(
            SelectionService& selection,
            const Comet::ComponentRegistry& component_registry,
            const PropertyEditorRegistry& property_editor_registry,
            const Comet::AssetDatabase& asset_database,
            std::filesystem::path assets_root,
            ReloadMaterialCallback reload_material_callback);

        void render() override;

    private:
        void render_entity(Comet::Entity entity) const;
        void render_asset(Comet::AssetHandle handle);
        void render_material(const Comet::AssetRecord& record);
        void load_asset(const Comet::AssetRecord& record);
        void save_material(const Comet::AssetRecord& record);
        [[nodiscard]] std::string validate_material() const;

        SelectionService& m_selection;
        const Comet::ComponentRegistry& m_component_registry;
        const PropertyEditorRegistry& m_property_editor_registry;
        const Comet::AssetDatabase& m_asset_database;
        std::filesystem::path m_assets_root;
        ReloadMaterialCallback m_reload_material_callback;
        Comet::AssetHandle m_loaded_asset;
        std::optional<Comet::MaterialData> m_material_data;
        bool m_material_dirty = false;
        std::string m_asset_error;
        std::string m_asset_status;
    };

}
