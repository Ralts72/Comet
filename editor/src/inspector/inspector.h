#pragma once

#include "asset/database.h"
#include "assets/asset_edit.h"
#include "ui/editor_panel.h"
#include "scene/command_history.h"
#include "scene/scene_environment.h"
#include "editor_state.h"
#include "assets/asset_reference.h"
#include "assets/material_editing.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Comet {
    class ComponentRegistry;
    class Entity;
    class MaterialLayout;
}

namespace CometEditor {
    class PropertyEditorRegistry;
    class SelectionService;

    class InspectorPanel: public EditorPanel {
    public:
        struct AssetAssignment {
            PropertyEditTransaction::Target target;
            AssetDragPayload asset;
        };
        InspectorPanel(const EditorState& state, SelectionService& selection,
            CommandHistory& history, PropertyEditTransaction& property_edit,
            const Comet::ComponentRegistry& component_registry,
            const PropertyEditorRegistry& property_editor_registry,
            const Comet::AssetDatabase& asset_database, std::filesystem::path assets_root);

        void render() override;
        [[nodiscard]] bool finish_edit(bool cancel = false);
        void set_material_layouts(
            std::vector<std::shared_ptr<const Comet::MaterialLayout>> layouts);
        [[nodiscard]] std::optional<AssetAssignment> take_asset_assignment();
        [[nodiscard]] std::optional<AssetEdit> take_asset_edit();
        void complete_asset_edit(const AssetEdit& edit, bool succeeded, std::string error = {});

    private:
        void render_scene(Comet::Scene& scene);
        void render_environment(Comet::Scene& scene);
        void render_post_process(Comet::Scene& scene);
        void render_entity(Comet::Entity entity);
        void render_property(Comet::Entity entity, const Comet::ComponentDescriptor& component,
            const Comet::PropertyDescriptor& property);
        void render_asset_property(const PropertyEditTransaction::Target& target,
            const Comet::PropertyDescriptor& property, Comet::AssetHandle handle);
        [[nodiscard]] std::optional<AssetDragPayload> accept_asset_drop(
            Comet::AssetType expected_type);
        void render_asset(Comet::AssetHandle handle);
        void render_texture(const Comet::AssetRecord& record);
        void render_material(const Comet::AssetRecord& record);
        void confirm_material_template();
        void load_asset(const Comet::AssetRecord& record);
        void reimport_texture(const Comet::AssetRecord& record,
            const Comet::TextureImportSettings& previous_settings);
        void update_material(
            const Comet::AssetRecord& record, const Comet::MaterialData& previous_data);
        [[nodiscard]] std::string validate_material() const;
        [[nodiscard]] std::shared_ptr<const Comet::MaterialLayout> material_layout() const;

        const EditorState& m_state;
        SelectionService& m_selection;
        CommandHistory& m_history;
        PropertyEditTransaction& m_property_edit;
        const Comet::ComponentRegistry& m_component_registry;
        const PropertyEditorRegistry& m_property_editor_registry;
        const Comet::AssetDatabase& m_asset_database;
        std::filesystem::path m_assets_root;
        Comet::AssetHandle m_loaded_asset;
        Comet::AssetRevision m_loaded_revision = 0;
        std::optional<Comet::TextureImportSettings> m_texture_import_settings;
        std::optional<Comet::MaterialData> m_material_data;
        std::vector<std::shared_ptr<const Comet::MaterialLayout>> m_material_layouts;
        std::string m_asset_error;
        std::optional<AssetAssignment> m_asset_assignment;
        std::optional<AssetEdit> m_asset_edit;
        std::optional<MaterialTemplateChange> m_template_change;
        uint32_t m_active_item = 0;
    };

}
