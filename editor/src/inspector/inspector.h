#pragma once

#include "asset/database.h"
#include "inspector/asset_inspector.h"
#include "ui/editor_panel.h"
#include "scene/command_history.h"
#include "scene/scene_settings.h"
#include "editor_state.h"
#include "assets/asset_reference.h"

#include <memory>
#include <optional>

namespace Comet {
    class AssetRegistry;
    class ComponentRegistry;
    class Entity;
    class Script;
}

namespace CometEditor {
    class PropertyEditorRegistry;
    class SelectionService;
    struct PropertyEditResult;

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
            const Comet::AssetDatabase& asset_database, const Comet::AssetRegistry& runtime_assets);

        void render() override;
        [[nodiscard]] bool finish_edit(bool cancel = false);
        [[nodiscard]] AssetInspector& asset_inspector() { return m_asset_inspector; }
        [[nodiscard]] std::optional<AssetAssignment> take_asset_assignment();

    private:
        template<typename Value>
        void apply_scene_edit(PropertyEditTransaction::SceneTarget<Value> target,
            const Value& value, const PropertyEditResult& result, bool can_edit);
        void render_scene(Comet::Scene& scene);
        void render_environment(Comet::Scene& scene);
        void render_post_process(Comet::Scene& scene);
        void render_entity(Comet::Entity entity);
        void render_property(Comet::Entity entity, const Comet::ComponentDescriptor& component,
            const Comet::PropertyDescriptor& property);
        void render_script_parameters(Comet::Entity entity,
            const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property);
        void apply_property_edit(Comet::Entity entity, const Comet::ComponentDescriptor& component,
            const Comet::PropertyDescriptor& property, const Comet::PropertyValue& value,
            const PropertyEditResult& result);
        void render_asset_property(const PropertyEditTransaction::Target& target,
            const Comet::PropertyDescriptor& property, Comet::AssetHandle handle);
        [[nodiscard]] std::optional<AssetDragPayload> accept_asset_drop(
            Comet::AssetType expected_type);

        const EditorState& m_state;
        SelectionService& m_selection;
        CommandHistory& m_history;
        PropertyEditTransaction& m_property_edit;
        const Comet::ComponentRegistry& m_component_registry;
        const PropertyEditorRegistry& m_property_editor_registry;
        const Comet::AssetDatabase& m_asset_database;
        const Comet::AssetRegistry& m_runtime_assets;
        std::shared_ptr<const Comet::Script> m_script_edit_version;
        AssetInspector m_asset_inspector;
        std::optional<AssetAssignment> m_asset_assignment;
        uint32_t m_active_item = 0;
    };

}
