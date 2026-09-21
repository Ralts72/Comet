#include "inspector/inspector.h"
#include "render/material/material_layout.h"
#include "inspector/property_editor_registry.h"
#include "scene/selection.h"
#include "scene/scene_commands.h"
#include "ui/dialogs.h"
#include "diagnostics/logger.h"

#include "render/material/material.h"
#include "scene/component_registry.h"
#include "scene/script_component.h"
#include "asset/registry.h"
#include "scripting/script.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <utility>

namespace CometEditor {
    namespace {
        const std::string& property_label(const auto& property) {
            if(property.display_name.empty())
                return property.name;
            return property.display_name;
        }
        constexpr PropertyEditTransaction::SceneTarget<Comet::SceneEnvironment> environment_target{
            &Comet::Scene::get_environment, &Comet::Scene::set_environment};
        constexpr PropertyEditTransaction::SceneTarget<Comet::PostProcessSettings>
            post_process_target{&Comet::Scene::get_post_process, &Comet::Scene::set_post_process};

        const char* texture_color_space_label(const Comet::TextureColorSpace color_space) {
            switch(color_space) {
                case Comet::TextureColorSpace::Srgb:
                    return "sRGB";
                case Comet::TextureColorSpace::Linear:
                    return "Linear";
            }
            return "Unknown";
        }
    }

    InspectorPanel::InspectorPanel(const EditorState& state, SelectionService& selection,
        CommandHistory& history, PropertyEditTransaction& property_edit,
        const Comet::ComponentRegistry& component_registry,
        const PropertyEditorRegistry& property_editor_registry,
        const Comet::AssetDatabase& asset_database, const Comet::AssetRegistry& runtime_assets)
        : EditorPanel("Inspector"), m_state(state), m_selection(selection), m_history(history),
          m_property_edit(property_edit), m_component_registry(component_registry),
          m_property_editor_registry(property_editor_registry), m_asset_database(asset_database),
          m_runtime_assets(runtime_assets) {
        const auto builtins = Comet::MaterialLayout::builtins();
        m_material_layouts.assign(builtins.begin(), builtins.end());
    }

    void InspectorPanel::set_material_layouts(
        std::vector<std::shared_ptr<const Comet::MaterialLayout>> layouts) {
        std::erase(layouts, nullptr);
        std::ranges::sort(layouts, {}, [](const auto& layout) { return layout->get_name(); });
        m_material_layouts = std::move(layouts);
        m_template_change.reset();
    }

    std::shared_ptr<const Comet::MaterialLayout> InspectorPanel::material_layout() const {
        if(!m_material_data)
            return nullptr;
        for(const auto& layout : m_material_layouts) {
            if(layout && layout->get_name() == m_material_data->template_name)
                return layout;
        }
        return nullptr;
    }

    void InspectorPanel::render() {
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
        } else if(const Comet::AssetHandle asset = m_selection.get_selected_asset()) {
            static_cast<void>(m_property_edit.commit());
            render_asset(asset);
        } else if(auto* scene = m_selection.get_selected_scene()) {
            if(!m_property_edit.editing_scene())
                static_cast<void>(finish_edit());
            render_scene(*scene);
        } else {
            static_cast<void>(m_property_edit.commit());
            ImGui::TextUnformatted(Ui::text("No entity or asset selected"));
        }

        confirm_material_template();
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
        if(auto effective = script->parameters(binding.parameters); !effective) {
            ImGui::TextWrapped("%s", effective.error().message.c_str());
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
            if(changed && !property.assign_value(component.get_component(entity), value)) {
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
        if(m_state.mode != EditorMode::Edit || !ImGui::BeginDragDropTarget())
            return std::nullopt;
        std::optional<AssetDragPayload> result;
        if(const auto payload = read_asset_drag_payload(ImGui::GetDragDropPayload())) {
            const auto& asset = *payload;
            const auto* record = m_asset_database.find(asset.handle);
            if(asset.type == expected_type && record && record->type == expected_type
                && m_asset_database.is_current(asset.handle, asset.revision)
                && asset.generation == m_history.generation()
                && ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE))
                result = asset;
        }
        ImGui::EndDragDropTarget();
        return result;
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

    void InspectorPanel::render_asset(const Comet::AssetHandle handle) {
        const Comet::AssetRecord* record = m_asset_database.find(handle);
        if(!record) {
            ImGui::TextColored(
                ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "Selected asset is no longer indexed");
            return;
        }

        if(m_loaded_asset != handle || !m_asset_database.is_current(handle, m_loaded_revision)) {
            load_asset(*record);
        }

        ImGui::Text(Ui::text("Path: %s"), record->path.generic_string().c_str());
        ImGui::Text(Ui::text("Type: %s"), Comet::to_string(record->type).data());
        ImGui::Separator();

        if(!m_asset_error.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "%s", m_asset_error.c_str());
        }

        if(record->type == Comet::AssetType::Material) {
            if(m_material_data) {
                render_material(*record);
            } else if(ImGui::Button(Ui::label("Retry Load").c_str())) {
                load_asset(*record);
            }
            return;
        }

        if(record->type == Comet::AssetType::Texture) {
            if(m_texture_import_settings) {
                render_texture(*record);
            } else if(ImGui::Button(Ui::label("Retry Load").c_str())) {
                load_asset(*record);
            }
            return;
        }

        ImGui::TextDisabled("%s", Ui::text("No inspector is available for this asset type"));
    }

    void InspectorPanel::render_texture(const Comet::AssetRecord& record) {
        std::optional<Comet::TextureImportSettings> previous_settings;
        const char* color_space = texture_color_space_label(m_texture_import_settings->color_space);
        if(ImGui::BeginCombo(Ui::label("Color Space").c_str(), Ui::text(color_space))) {
            constexpr std::array color_spaces{
                Comet::TextureColorSpace::Srgb, Comet::TextureColorSpace::Linear};
            for(const Comet::TextureColorSpace candidate : color_spaces) {
                const bool selected = candidate == m_texture_import_settings->color_space;
                const char* label = texture_color_space_label(candidate);
                if(ImGui::Selectable(Ui::label(label).c_str(), selected) && !selected) {
                    if(!previous_settings) {
                        previous_settings = *m_texture_import_settings;
                    }
                    m_texture_import_settings->color_space = candidate;
                }
                if(selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        bool flip_y = m_texture_import_settings->flip_y;
        if(ImGui::Checkbox(Ui::label("Flip Y").c_str(), &flip_y)) {
            if(!previous_settings) {
                previous_settings = *m_texture_import_settings;
            }
            m_texture_import_settings->flip_y = flip_y;
        }

        if(previous_settings) {
            reimport_texture(record, *previous_settings);
        }
    }

    void InspectorPanel::render_material(const Comet::AssetRecord& record) {
        std::optional<Comet::MaterialData> previous_data;
        const auto layout = material_layout();
        if(ImGui::BeginCombo(
               Ui::label("Template").c_str(), m_material_data->template_name.c_str())) {
            for(const auto& candidate : m_material_layouts) {
                const bool selected = candidate->get_name() == m_material_data->template_name;
                if(ImGui::Selectable(candidate->get_name().c_str(), selected) && !selected)
                    m_template_change =
                        change_material_template(*m_material_data, layout.get(), *candidate);
                if(selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if(!layout) {
            ImGui::TextDisabled("%s", Ui::text("No registered layout for this material"));
            return;
        }
        const auto remember_previous = [&] {
            if(!previous_data)
                previous_data = *m_material_data;
        };
        ImGui::BeginDisabled(m_template_change.has_value());
        if(!layout->get_textures().empty())
            ImGui::SeparatorText(Ui::text("Textures"));

        for(const auto& property : layout->get_textures()) {
            const auto& property_name = property.name;
            const auto found = m_material_data->texture_properties.find(property_name);
            Comet::AssetHandle texture_handle;
            if(found != m_material_data->texture_properties.end())
                texture_handle = found->second;
            ImGui::PushID(property_name.c_str());
            const auto& label = property_label(property);
            const auto assign = [&](Comet::AssetHandle value) {
                if(value == texture_handle)
                    return;
                remember_previous();
                m_material_data->scalar_properties.erase(property_name);
                m_material_data->vector_properties.erase(property_name);
                if(value)
                    m_material_data->texture_properties[property_name] = value;
                else
                    m_material_data->texture_properties.erase(property_name);
                texture_handle = value;
            };
            auto selected = texture_handle;
            if(edit_asset_reference(label.c_str(), selected, m_asset_database,
                   Comet::AssetType::Texture, property.optional)) {
                assign(selected);
            }
            if(const auto asset = accept_asset_drop(Comet::AssetType::Texture);
                asset && asset->handle != texture_handle) {
                assign(asset->handle);
            }
            ImGui::PopID();
        }

        if(!layout->get_scalars().empty() || !layout->get_vectors().empty())
            ImGui::SeparatorText(Ui::text("Parameters"));
        for(const auto& property : layout->get_scalars()) {
            const auto found = m_material_data->scalar_properties.find(property.name);
            float value = property.default_value;
            if(found != m_material_data->scalar_properties.end())
                value = found->second;
            const float before = value;
            const auto& label = property_label(property);
            ImGui::PushID(property.name.c_str());
            if(ImGui::DragFloat(Ui::label(label.c_str()).c_str(), &value, property.step,
                   property.min_value, property.max_value, "%.3f", ImGuiSliderFlags_AlwaysClamp)
                && value != before) {
                remember_previous();
                m_material_data->texture_properties.erase(property.name);
                m_material_data->vector_properties.erase(property.name);
                m_material_data->scalar_properties[property.name] = value;
            }
            ImGui::PopID();
        }
        for(const auto& property : layout->get_vectors()) {
            const auto found = m_material_data->vector_properties.find(property.name);
            auto value = property.default_value;
            if(found != m_material_data->vector_properties.end())
                value = found->second;
            const auto before = value;
            const auto& label = property_label(property);
            ImGui::PushID(property.name.c_str());
            bool changed = false;
            if(property.semantic == Comet::MaterialLayout::VectorProperty::Semantic::Color) {
                changed = ImGui::ColorEdit4(
                    Ui::label(label.c_str()).c_str(), &value.x, ImGuiColorEditFlags_Float);
            } else {
                changed = ImGui::DragFloat4(Ui::label(label.c_str()).c_str(), &value.x, 0.01f);
            }
            if(changed && value != before) {
                remember_previous();
                m_material_data->texture_properties.erase(property.name);
                m_material_data->scalar_properties.erase(property.name);
                m_material_data->vector_properties[property.name] = value;
            }
            ImGui::PopID();
        }

        const std::string validation_error = validate_material();
        if(!validation_error.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "%s", validation_error.c_str());
        }

        if(previous_data && validation_error.empty()) {
            update_material(record, *previous_data);
        }
        ImGui::EndDisabled();
    }

    void InspectorPanel::confirm_material_template() {
        if(m_selection.get_selected_asset() != m_loaded_asset
            || !m_asset_database.is_current(m_loaded_asset, m_loaded_revision))
            m_template_change.reset();
        std::string template_name;
        std::span<const std::string> discarded;
        if(m_template_change) {
            template_name = m_template_change->data.template_name;
            discarded = m_template_change->discarded_properties;
        }
        const auto decision =
            draw_material_template_dialog(m_template_change.has_value(), template_name, discarded);
        if(!decision || !m_template_change)
            return;
        if(*decision) {
            const auto previous = *m_material_data;
            *m_material_data = m_template_change->data;
            const auto error = validate_material();
            if(error.empty())
                update_material(*m_asset_database.find(m_loaded_asset), previous);
            else {
                *m_material_data = previous;
                m_asset_error = error;
            }
        }
        m_template_change.reset();
    }

    void InspectorPanel::load_asset(const Comet::AssetRecord& record) {
        m_loaded_asset = record.handle;
        m_loaded_revision = m_asset_database.get_revision(record.handle);
        m_texture_import_settings.reset();
        m_material_data.reset();
        m_template_change.reset();
        m_asset_error.clear();

        if(record.type == Comet::AssetType::Texture) {
            const auto* settings =
                std::get_if<Comet::TextureImportSettings>(&record.import_settings);
            if(settings) {
                m_texture_import_settings = *settings;
            } else {
                m_asset_error = "Texture has incompatible import settings";
            }
            return;
        }

        if(record.type != Comet::AssetType::Material) {
            return;
        }

        m_asset_read = AssetRead{record.handle, m_loaded_revision};
    }

    std::optional<AssetRead> InspectorPanel::take_asset_read() {
        return std::exchange(m_asset_read, std::nullopt);
    }

    void InspectorPanel::complete_asset_read(
        const AssetRead& request, Comet::Result<Comet::MaterialData> result) {
        if(request.handle != m_loaded_asset || request.revision != m_loaded_revision
            || m_selection.get_selected_asset() != request.handle
            || !m_asset_database.is_current(request.handle, request.revision))
            return;
        if(result)
            m_material_data = std::move(result).value();
        else
            m_asset_error = result.error();
    }

    void InspectorPanel::reimport_texture(
        const Comet::AssetRecord& record, const Comet::TextureImportSettings& previous_settings) {
        if(!m_texture_import_settings) {
            return;
        }
        m_asset_edit = AssetEdit{record.handle, m_loaded_revision,
            TextureEdit{previous_settings, *m_texture_import_settings}};
    }

    void InspectorPanel::update_material(
        const Comet::AssetRecord& record, const Comet::MaterialData& previous_data) {
        if(!m_material_data) {
            return;
        }

        m_asset_edit = AssetEdit{
            record.handle, m_loaded_revision, MaterialEdit{previous_data, *m_material_data}};
    }

    std::optional<AssetEdit> InspectorPanel::take_asset_edit() {
        return std::exchange(m_asset_edit, std::nullopt);
    }

    void InspectorPanel::complete_asset_edit(
        const AssetEdit& edit, const bool succeeded, std::string error) {
        if(m_loaded_asset != edit.handle || m_loaded_revision != edit.revision)
            return;
        m_asset_error = std::move(error);
        if(succeeded)
            return;
        if(const auto* material = std::get_if<MaterialEdit>(&edit.value))
            m_material_data = material->before;
        else
            m_texture_import_settings = std::get<TextureEdit>(edit.value).before;
    }

    std::string InspectorPanel::validate_material() const {
        if(!m_material_data)
            return "Material data is not loaded";
        const auto layout = material_layout();
        if(!layout)
            return "Material layout is not registered";
        const auto result = validate_material_data(*m_material_data, *layout, m_asset_database);
        if(!result)
            return result.error();
        return {};
    }
}
