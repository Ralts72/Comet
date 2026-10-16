#pragma once

#include "asset/database.h"
#include "scene/component_registry.h"
#include "inspector/asset_inspector.h"
#include "inspector/animation_preview.h"
#include "inspector/particle_preview.h"
#include "ui/editor_panel.h"
#include "scene/command_history.h"
#include "scene/scene_settings.h"
#include "editor_state.h"
#include "assets/asset_reference.h"

#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    class AssetRegistry;
    class MaterialProgramValidator;
    class Entity;
    class Script;
    class SceneRuntime;
}

namespace CometEditor {
    class PropertyEditorRegistry;
    class SelectionService;
    class ReflectionBake;
    class IrradianceBake;
    struct PropertyEditResult;

    class InspectorPanel: public EditorPanel {
    public:
        struct AssetAssignment {
            std::vector<PropertyEditTransaction::Target> targets;
            AssetDragPayload asset;
        };
        struct LightingBakeRequest {
            enum class Kind { Reflection, Irradiance };
            Kind kind;
            Comet::EntityUuid entity;
            uint64_t generation;
        };
        InspectorPanel(const EditorState& state, SelectionService& selection,
            CommandHistory& history, PropertyEditTransaction& property_edit,
            const Comet::ComponentRegistry& component_registry,
            const PropertyEditorRegistry& property_editor_registry,
            const Comet::AssetDatabase& asset_database, const Comet::AssetRegistry& runtime_assets,
            const Comet::MaterialProgramValidator& programs,
            const Comet::SceneRuntime* runtime = nullptr, ReflectionBake* reflection_bake = nullptr,
            IrradianceBake* irradiance_bake = nullptr);
        ~InspectorPanel() override;

        void render() override;
        [[nodiscard]] bool finish_edit(bool cancel = false);
        void reset_for_scene_change();
        void update_previews(double delta_time);
        [[nodiscard]] AssetInspector& asset_inspector() { return m_asset_inspector; }
        [[nodiscard]] std::optional<AssetAssignment> take_asset_assignment();
        [[nodiscard]] std::optional<LightingBakeRequest> take_lighting_bake_request();

    private:
        template<typename Value>
        void apply_scene_edit(PropertyEditTransaction::SceneTarget<Value> target,
            const Value& value, const PropertyEditResult& result, bool can_edit);
        void render_scene(Comet::Scene& scene);
        void render_environment(Comet::Scene& scene);
        void render_post_process(Comet::Scene& scene);
        void render_entity(Comet::Entity entity);
        void render_animation_preview(Comet::Entity entity);
        [[nodiscard]] bool bind_animation_preview(Comet::Entity entity);
        void reset_animation_preview();
        void update_animation_preview(double delta_time);
        void render_particle_preview(Comet::Entity entity);
        bool bind_particle_preview(Comet::Entity entity);
        void reset_particle_preview();
        void render_controller_editor();
        void render_property(Comet::Entity entity, const Comet::ComponentDescriptor& component,
            const Comet::PropertyDescriptor& property);
        void render_animation_parameters(Comet::Entity entity,
            const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property);
        void render_script_parameters(Comet::Entity entity,
            const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property);
        void apply_property_edit(Comet::Entity entity, const Comet::ComponentDescriptor& component,
            const Comet::PropertyDescriptor& property, const Comet::PropertyValue& value,
            const PropertyEditResult& result);
        [[nodiscard]] std::vector<PropertyEditTransaction::Target> property_targets(
            std::string_view component, std::string_view property) const;
        void apply_property_values(std::span<const PropertyEditTransaction::Target> targets,
            std::span<const Comet::PropertyValue> values, const PropertyEditResult& result);
        void render_asset_property(const PropertyEditTransaction::Target& target,
            const Comet::PropertyDescriptor& property, Comet::AssetHandle handle);
        [[nodiscard]] std::optional<AssetDragPayload> accept_asset_drop(
            Comet::AssetType expected_type);

        const EditorState& m_state;
        SelectionService& m_selection;
        CommandHistory& m_history;
        PropertyEditTransaction& m_property_edit;
        const Comet::ComponentRegistry& m_component_registry;
        const std::vector<Comet::ComponentDescriptor> m_component_descriptions;
        const PropertyEditorRegistry& m_property_editor_registry;
        const Comet::AssetDatabase& m_asset_database;
        const Comet::AssetRegistry& m_runtime_assets;
        const Comet::SceneRuntime* m_runtime;
        ReflectionBake* m_reflection_bake;
        IrradianceBake* m_irradiance_bake;
        std::shared_ptr<const Comet::Script> m_script_edit_version;
        AssetInspector m_asset_inspector;
        std::optional<AssetAssignment> m_asset_assignment;
        std::optional<LightingBakeRequest> m_lighting_bake_request;
        std::vector<Comet::EntityUuid> m_entities;
        AnimationPreview m_animation_preview;
        std::shared_ptr<const Comet::AnimationPlayback> m_preview_publication;
        bool m_animation_preview_drawn = false;
        ParticlePreview m_particle_preview;
        std::shared_ptr<const Comet::ParticleSnapshot> m_particle_publication;
        bool m_particle_preview_drawn = false;
        uint32_t m_active_item = 0;
        uint32_t m_script_active_item = 0;
    };

}
