#include "inspector/inspector.h"
#include "inspector/property_editor_registry.h"
#include "scene/selection.h"
#include "scene/scene_commands.h"
#include "diagnostics/logger.h"

#include "scene/component_registry.h"
#include "scene/script_component.h"
#include "asset/registry.h"
#include "scripting/script.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <utility>

namespace CometEditor {
    namespace {
        constexpr PropertyEditTransaction::SceneTarget<Comet::SceneEnvironment> environment_target{
            &Comet::Scene::get_environment, &Comet::Scene::set_environment};
        constexpr PropertyEditTransaction::SceneTarget<Comet::PostProcessSettings>
            post_process_target{&Comet::Scene::get_post_process, &Comet::Scene::set_post_process};

    }

    InspectorPanel::InspectorPanel(const EditorState& state, SelectionService& selection,
        CommandHistory& history, PropertyEditTransaction& property_edit,
        const Comet::ComponentRegistry& component_registry,
        const PropertyEditorRegistry& property_editor_registry,
        const Comet::AssetDatabase& asset_database, const Comet::AssetRegistry& runtime_assets,
        const Comet::MaterialPrograms& programs)
        : EditorPanel("Inspector"), m_state(state), m_selection(selection), m_history(history),
          m_property_edit(property_edit), m_component_registry(component_registry),
          m_property_editor_registry(property_editor_registry), m_asset_database(asset_database),
          m_runtime_assets(runtime_assets), m_asset_inspector(asset_database, programs) {}

    void InspectorPanel::render() {
        m_asset_inspector.select(m_selection.get_selected_asset());
        m_asset_assignment.reset();
        if(m_property_edit.editing_scene()
            && (!m_user_visible || !m_selection.get_selected_scene()
                || m_selection.get_selected_scene() != m_history.get_scene()))
            static_cast<void>(finish_edit());
        if(!m_user_visible) {
            static_cast<void>(finish_edit());
            return;
        }

        if(!ImGui::Begin(window_label().c_str(), &m_user_visible)) {
            static_cast<void>(finish_edit());
            ImGui::End();
            return;
        }

        if(Comet::Entity entity = m_selection.get_selected_entity()) {
            render_entity(entity);
        } else if(m_selection.get_selected_asset()) {
            static_cast<void>(m_property_edit.commit());
            m_asset_inspector.render(m_history.generation(), m_state.mode == EditorMode::Edit);
        } else if(auto* scene = m_selection.get_selected_scene()) {
            if(!m_property_edit.editing_scene())
                static_cast<void>(finish_edit());
            render_scene(*scene);
        } else {
            static_cast<void>(m_property_edit.commit());
            ImGui::TextUnformatted(Ui::text("No entity or asset selected"));
        }

        ImGui::End();
    }

    void InspectorPanel::render_entity(Comet::Entity entity) {
        ImGui::Text(Ui::text("Entity ID: %llu"), static_cast<unsigned long long>(entity.get_id()));

        bool active_property_visible = false;
        const bool edit_structure =
            m_state.mode == EditorMode::Edit && m_history.get_scene()
            && m_history.get_scene()->find_entity(entity.get_uuid()) == entity;
        const Comet::ComponentDescriptor* remove = nullptr;
        for(const Comet::ComponentDescriptor& component_descriptor :
            m_component_registry.components()) {
            if(!component_descriptor.has_component(entity)) {
                continue;
            }

            ImGui::PushID(component_descriptor.id.c_str());
            const bool is_name = component_descriptor.id == "name";
            const bool expanded = is_name
                                  || ImGui::CollapsingHeader(
                                      Ui::label(component_descriptor.display_name.c_str()).c_str(),
                                      ImGuiTreeNodeFlags_DefaultOpen);
            if(!is_name && ImGui::BeginPopupContextItem("Component actions")) {
                if(ImGui::MenuItem(Ui::label("Remove Component").c_str(), nullptr, false,
                       edit_structure
                           && SceneCommands::can_edit_component_structure(component_descriptor)))
                    remove = &component_descriptor;
                ImGui::EndPopup();
            }
            if(expanded) {
                for(const Comet::PropertyDescriptor& property : component_descriptor.properties) {
                    ImGui::PushID(property.id.c_str());
                    if(component_descriptor.id == "script"
                        && property.type == Comet::PropertyType::Parameters)
                        render_script_parameters(entity, component_descriptor, property);
                    else
                        render_property(entity, component_descriptor, property);
                    active_property_visible |= m_property_edit.targets(
                        {entity.get_uuid(), component_descriptor.id, property.id});
                    ImGui::PopID();
                }
            }
            ImGui::PopID();
            if(is_name) {
                ImGui::Separator();
            }
        }
        if(!active_property_visible && !m_property_edit.commit()) {
            LOG_ERROR("Cannot finish hidden property edit");
            return;
        }
        const Comet::ComponentDescriptor* add = nullptr;
        ImGui::BeginDisabled(!edit_structure);
        if(ImGui::Button(Ui::label("Add Component").c_str()))
            ImGui::OpenPopup("Add Component");
        if(ImGui::BeginPopup("Add Component")) {
            for(const auto& component : m_component_registry.components()) {
                if(SceneCommands::can_edit_component_structure(component)
                    && !component.has_component(entity)
                    && ImGui::MenuItem(Ui::label(component.display_name.c_str()).c_str()))
                    add = &component;
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        // 结束本轮属性访问后再修改结构，避免移除正在访问的组件。
        if(add || remove) {
            if(!m_property_edit.commit()) {
                LOG_ERROR("Cannot finish property edit before component change");
                return;
            }
            bool changed = false;
            if(add)
                changed = SceneCommands::add_component(
                    m_history, m_component_registry, entity.get_uuid(), add->id);
            else
                changed = SceneCommands::remove_component(
                    m_history, m_component_registry, entity.get_uuid(), remove->id);
            if(!changed)
                LOG_ERROR("Cannot change component structure");
        }
    }

    void InspectorPanel::render_scene(Comet::Scene& scene) {
        render_environment(scene);
        render_post_process(scene);
    }

    template<typename Value>
    void InspectorPanel::apply_scene_edit(PropertyEditTransaction::SceneTarget<Value> target,
        const Value& value, const PropertyEditResult& result, bool can_edit) {
        if(!can_edit)
            return;
        if(result.active)
            m_active_item = ImGui::GetActiveID();
        if(ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            static_cast<void>(finish_edit(true));
            return;
        }
        if((result.began || result.changed) && !m_property_edit.begin(target))
            return;
        if(result.changed && !m_property_edit.preview(target, value))
            LOG_WARN("Invalid scene setting; previous preview retained");
        if(result.finished && m_property_edit.targets(target))
            static_cast<void>(finish_edit());
    }

    void InspectorPanel::render_environment(Comet::Scene& scene) {
        ImGui::SeparatorText(Ui::text("Environment"));
        const bool can_edit = m_state.mode == EditorMode::Edit && m_history.get_scene() == &scene;
        if(!can_edit)
            static_cast<void>(finish_edit(true));
        ImGui::BeginDisabled(!can_edit);
        auto environment = scene.get_environment();
        PropertyEditResult result;
        result.changed = edit_asset_reference(
            "HDR map", environment.asset, m_asset_database, Comet::AssetType::Environment);
        if(const auto asset = accept_asset_drop(Comet::AssetType::Environment)) {
            environment.asset = asset->handle;
            result.changed = true;
        }
        result.changed |= ImGui::Checkbox(Ui::label("Background").c_str(), &environment.background);
        result.changed |= ImGui::Checkbox(Ui::label("Lighting").c_str(), &environment.lighting);
        result.finished = result.changed;
        result.include_item(ImGui::ColorEdit3(Ui::label("Background color").c_str(),
            &environment.background_color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR));
        result.include_item(
            ImGui::DragFloat(Ui::label("Intensity").c_str(), &environment.intensity, 0.02f, 0.0f,
                Comet::SceneEnvironment::MAX_INTENSITY, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        result.include_item(ImGui::DragFloat(Ui::label("Lighting intensity").c_str(),
            &environment.lighting_intensity, 0.02f, 0.0f, Comet::SceneEnvironment::MAX_INTENSITY,
            "%.2f", ImGuiSliderFlags_AlwaysClamp));
        result.include_item(ImGui::DragFloat(
            Ui::label("Rotation").c_str(), &environment.rotation, 0.5f, 0.0f, 0.0f, "%.1f deg"));
        apply_scene_edit(environment_target, environment, result, can_edit);
        ImGui::EndDisabled();
    }

    void InspectorPanel::render_post_process(Comet::Scene& scene) {
        ImGui::SeparatorText(Ui::text("Post Processing"));
        const bool can_edit = m_state.mode == EditorMode::Edit && m_history.get_scene() == &scene;
        ImGui::BeginDisabled(!can_edit);
        auto settings = scene.get_post_process();
        PropertyEditResult result;
        result.include_item(
            ImGui::DragFloat(Ui::label("Exposure").c_str(), &settings.exposure, 0.02f, 0.0f,
                Comet::PostProcessSettings::MAX_EXPOSURE, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        const bool toggled = ImGui::Checkbox(Ui::label("Bloom").c_str(), &settings.bloom_enabled);
        result.changed |= toggled;
        result.finished |= toggled;
        ImGui::BeginDisabled(!settings.bloom_enabled);
        result.include_item(ImGui::DragFloat(Ui::label("Bloom strength").c_str(),
            &settings.bloom_strength, 0.01f, 0.0f, Comet::PostProcessSettings::MAX_BLOOM_STRENGTH,
            "%.2f", ImGuiSliderFlags_AlwaysClamp));
        result.include_item(ImGui::DragFloat(Ui::label("Bloom threshold").c_str(),
            &settings.bloom_threshold, 0.05f, 0.0f, Comet::PostProcessSettings::MAX_BLOOM_THRESHOLD,
            "%.2f", ImGuiSliderFlags_AlwaysClamp));
        ImGui::EndDisabled();
        apply_scene_edit(post_process_target, settings, result, can_edit);
        ImGui::EndDisabled();
    }

    bool InspectorPanel::finish_edit(const bool cancel) {
        if(m_active_item && ImGui::GetCurrentContext() && ImGui::GetActiveID() == m_active_item)
            ImGui::ClearActiveID();
        m_active_item = 0;
        if(cancel)
            return m_property_edit.cancel();
        return m_property_edit.commit();
    }

    void InspectorPanel::render_property(Comet::Entity entity,
        const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property) {
        if(!property.editable || property.read_only)
            return;
        auto value = property.copy_value(component.get_component(std::as_const(entity)));
        if(!value)
            return;
        if(property.type == Comet::PropertyType::AssetHandle && property.asset_type) {
            render_asset_property({entity.get_uuid(), component.id, property.id}, property,
                std::get<Comet::AssetHandle>(*value));
            return;
        }
        if(!m_property_editor_registry.contains(property.type))
            return;
        const PropertyEditResult result = std::visit(
            [&](auto& edited) {
                return m_property_editor_registry.edit_property(property, &edited);
            },
            *value);
        apply_property_edit(entity, component, property, *value, result);
    }

    void InspectorPanel::render_script_parameters(Comet::Entity entity,
        const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property) {
        const auto& binding = entity.get_component<Comet::ScriptComponent>();
        std::shared_ptr<const Comet::Script> script;
        if(m_state.mode == EditorMode::Play)
            script = binding.running_script();
        else
            script = m_runtime_assets.resolve<Comet::Script>(binding.asset);
        const PropertyEditTransaction::Target target{entity.get_uuid(), component.id, property.id};
        if(m_property_edit.targets(target) && m_script_edit_version != script) {
            if(!m_property_edit.cancel()) {
                LOG_ERROR("Cannot cancel parameter edit after script definition changed");
                return;
            }
            ImGui::ClearActiveID();
        }
        m_script_edit_version = script;
        if(!binding.asset)
            return;
        if(!script) {
            ImGui::TextDisabled("%s", Ui::text("Script unavailable; see Console"));
            return;
        }
        if(ImGui::Button(Ui::label("Restore default parameters").c_str())) {
            apply_property_edit(entity, component, property, Comet::ParameterMap{},
                {.changed = true, .finished = true});
            return;
        }
        if(auto checked = script->validate_overrides(binding.parameters); !checked) {
            ImGui::TextWrapped("%s", checked.error().message.c_str());
            return;
        }
        auto overrides = binding.parameters;
        const auto result =
            m_property_editor_registry.edit_parameters(script->defaults(), overrides);
        apply_property_edit(entity, component, property, overrides, result);
    }

    void InspectorPanel::apply_property_edit(Comet::Entity entity,
        const Comet::ComponentDescriptor& component, const Comet::PropertyDescriptor& property,
        const Comet::PropertyValue& value, const PropertyEditResult& result) {
        const PropertyEditTransaction::Target target{entity.get_uuid(), component.id, property.id};
        const bool changed = result.changed;
        const bool active = result.active;
        const bool activated = result.began;
        const bool deactivated = result.finished;
        if(active) {
            m_active_item = result.active_item;
            if(!m_active_item)
                m_active_item = ImGui::GetItemID();
        }
        if(m_state.mode == EditorMode::Play) {
            // Play 中仍可调试 Runtime 属性，但不写入 Edit 文档历史。
            if(changed && !component.assign_property(entity, property.id, value)) {
                LOG_ERROR("Cannot update runtime property");
            }
            return;
        }

        if((activated || changed) && !m_property_edit.targets(target)) {
            if(!m_property_edit.begin(target)) {
                LOG_ERROR("Cannot begin edit of {}.{}", component.id, property.id);
                return;
            }
        }
        if(m_property_edit.targets(target)) {
            if(ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                if(!m_property_edit.cancel())
                    LOG_ERROR("Cannot cancel property edit");
                ImGui::ClearActiveID();
                return;
            }
            if(changed && !m_property_edit.preview(value)) {
                LOG_ERROR("Cannot preview edit of {}.{}", component.id, property.id);
            }
            if(deactivated || (changed && !active)) {
                if(!m_property_edit.commit())
                    LOG_ERROR("Cannot commit property edit");
            }
        }
    }

    std::optional<InspectorPanel::AssetAssignment> InspectorPanel::take_asset_assignment() {
        return std::exchange(m_asset_assignment, std::nullopt);
    }

    std::optional<AssetDragPayload> InspectorPanel::accept_asset_drop(
        const Comet::AssetType expected_type) {
        if(m_state.mode != EditorMode::Edit)
            return std::nullopt;
        return CometEditor::accept_asset_drop(
            m_asset_database, expected_type, m_history.generation());
    }

    void InspectorPanel::render_asset_property(const PropertyEditTransaction::Target& target,
        const Comet::PropertyDescriptor& property, const Comet::AssetHandle handle) {
        auto selected = handle;
        const auto type = *property.asset_type;
        if(edit_asset_reference(property.display_name.c_str(), selected, m_asset_database, type))
            m_asset_assignment = AssetAssignment{target,
                {selected, m_asset_database.get_revision(selected), m_history.generation(), type}};
        if(const auto asset = accept_asset_drop(type); asset && asset->handle != handle)
            m_asset_assignment = AssetAssignment{target, *asset};
    }

}
