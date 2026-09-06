#include "inspector.h"
#include "property_editor_registry.h"
#include "selection.h"
#include "scene_commands.h"
#include "diagnostics/logger.h"

#include "asset/serialization/material_serializer.h"
#include "scene/component_registry.h"
#include "render/material_runtime.h"

#include <algorithm>
#include <array>
#include <exception>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <utility>
#include <vector>

namespace CometEditor {
    namespace {
        std::string texture_preview(
            const Comet::AssetDatabase& database, const Comet::AssetHandle handle) {
            const Comet::AssetRecord* record = database.find(handle);
            if(!record) {
                return "Missing texture (" + std::to_string(handle.value()) + ")";
            }
            if(record->type != Comet::AssetType::Texture) {
                return "Invalid asset type (" + std::to_string(handle.value()) + ")";
            }
            return record->path.generic_string();
        }

        const char* texture_color_space_label(
            const Comet::TextureColorSpace color_space) {
            switch(color_space) {
                case Comet::TextureColorSpace::Srgb:
                    return "sRGB";
                case Comet::TextureColorSpace::Linear:
                    return "Linear";
            }
            return "Unknown";
        }
    }

    InspectorPanel::InspectorPanel(SelectionService& selection, CommandHistory& history,
        PropertyEditTransaction& property_edit,
        const Comet::ComponentRegistry& component_registry,
        const PropertyEditorRegistry& property_editor_registry,
        const Comet::AssetDatabase& asset_database, std::filesystem::path assets_root,
        UpdateMaterialCallback update_material_callback,
        ReimportTextureCallback reimport_texture_callback)
        : EditorPanel("Inspector"), m_selection(selection), m_history(history),
          m_property_edit(property_edit), m_component_registry(component_registry),
          m_property_editor_registry(property_editor_registry),
          m_asset_database(asset_database), m_assets_root(std::move(assets_root)),
          m_update_material_callback(std::move(update_material_callback)),
          m_reimport_texture_callback(std::move(reimport_texture_callback)) {}

    void InspectorPanel::render() {
        m_asset_assignment.reset();
        if(!m_user_visible) {
            static_cast<void>(m_property_edit.commit());
            return;
        }

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            static_cast<void>(m_property_edit.commit());
            ImGui::End();
            return;
        }

        if(Comet::Entity entity = m_selection.get_selected_entity()) {
            render_entity(entity);
        } else if(const Comet::AssetHandle asset = m_selection.get_selected_asset()) {
            static_cast<void>(m_property_edit.commit());
            render_asset(asset);
        } else {
            static_cast<void>(m_property_edit.commit());
            ImGui::TextUnformatted("No entity or asset selected");
        }

        ImGui::End();
    }

    void InspectorPanel::invalidate_asset_cache() {
        m_loaded_asset = Comet::INVALID_ASSET_HANDLE;
        m_texture_import_settings.reset();
        m_material_data.reset();
        m_texture_assets.clear();
        m_asset_error.clear();
    }

    void InspectorPanel::render_entity(Comet::Entity entity) {
        ImGui::Text("Entity ID: %llu", static_cast<unsigned long long>(entity.get_id()));

        bool active_property_visible = false;
        const bool edit_structure =
            m_history.get_scene()
            && m_history.get_scene()->find_entity(entity.get_uuid()) == entity;
        const Comet::ComponentDescriptor* remove = nullptr;
        for(const Comet::ComponentDescriptor& component_descriptor :
            m_component_registry.components()) {
            if(!component_descriptor.has_component(entity)) {
                continue;
            }

            ImGui::PushID(component_descriptor.id.c_str());
            const bool is_name = component_descriptor.id == "name";
            const bool expanded =
                is_name
                || ImGui::CollapsingHeader(component_descriptor.display_name.c_str(),
                    ImGuiTreeNodeFlags_DefaultOpen);
            if(!is_name && ImGui::BeginPopupContextItem("Component actions")) {
                if(ImGui::MenuItem("Remove Component", nullptr, false,
                       edit_structure
                           && SceneCommands::can_edit_component_structure(
                               component_descriptor)))
                    remove = &component_descriptor;
                ImGui::EndPopup();
            }
            if(expanded) {
                for(const Comet::PropertyDescriptor& property :
                    component_descriptor.properties) {
                    ImGui::PushID(property.id.c_str());
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
        }
        const Comet::ComponentDescriptor* add = nullptr;
        ImGui::BeginDisabled(!edit_structure);
        if(ImGui::Button("Add Component"))
            ImGui::OpenPopup("Add Component");
        if(ImGui::BeginPopup("Add Component")) {
            for(const auto& component : m_component_registry.components()) {
                if(SceneCommands::can_edit_component_structure(component)
                    && !component.has_component(entity)
                    && ImGui::MenuItem(component.display_name.c_str()))
                    add = &component;
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        // 结束本轮属性访问后再修改结构，避免移除正在访问的组件。
        if((add || remove) && m_property_edit.commit()) {
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

    void InspectorPanel::render_property(Comet::Entity entity,
        const Comet::ComponentDescriptor& component,
        const Comet::PropertyDescriptor& property) {
        if(!property.editable || property.read_only)
            return;
        auto value = property.copy_value(component.get_component(entity));
        if(!value)
            return;
        const PropertyEditTransaction::Target target{
            entity.get_uuid(), component.id, property.id};
        if(property.type == Comet::PropertyType::AssetHandle && property.asset_type) {
            render_asset_property(target, property, std::get<Comet::AssetHandle>(*value));
            return;
        }
        if(!m_property_editor_registry.contains(property.type))
            return;
        const bool changed = std::visit(
            [&](auto& edited) {
                return m_property_editor_registry.edit_property(property, &edited);
            },
            *value);
        const bool active = ImGui::IsItemActive();
        const bool activated = ImGui::IsItemActivated();
        const bool deactivated = ImGui::IsItemDeactivated();
        if(m_history.get_scene() == nullptr) {
            // Play 中仍可调试 Runtime 属性，但不写入 Edit 文档历史。
            if(changed
                && !property.assign_value(component.get_component(entity), *value)) {
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
            if(changed && !m_property_edit.preview(*value)) {
                LOG_ERROR("Cannot preview edit of {}.{}", component.id, property.id);
            }
            if(deactivated || (changed && !active)) {
                if(!m_property_edit.commit())
                    LOG_ERROR("Cannot commit property edit");
            }
        }
    }

    std::optional<InspectorPanel::AssetAssignment> InspectorPanel::
        take_asset_assignment() {
        return std::exchange(m_asset_assignment, std::nullopt);
    }

    std::optional<AssetDragPayload> InspectorPanel::accept_asset_drop(
        const Comet::AssetType expected_type) {
        std::optional<AssetDragPayload> result;
        if(ImGui::BeginDragDropTarget()) {
            const auto* payload = ImGui::GetDragDropPayload();
            if(payload && payload->IsDataType(AssetDragPayload::TYPE)
                && payload->DataSize == sizeof(AssetDragPayload)) {
                const auto asset = *static_cast<const AssetDragPayload*>(payload->Data);
                const auto* record = m_asset_database.find(asset.handle);
                if(asset.type == expected_type && record && record->type == expected_type
                    && asset.generation == m_history.generation()
                    && ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE))
                    result = asset;
            }
            ImGui::EndDragDropTarget();
        }
        return result;
    }

    void InspectorPanel::render_asset_property(
        const PropertyEditTransaction::Target& target,
        const Comet::PropertyDescriptor& property, const Comet::AssetHandle handle) {
        const auto type = *property.asset_type;
        const auto* record = m_asset_database.find(handle);
        std::string preview = "None";
        if(handle) {
            if(record && record->type == type)
                preview = record->path.generic_string();
            else
                preview = "Missing / invalid (" + std::to_string(handle.value()) + ")";
        }
        const auto request = [&](const Comet::AssetHandle selected) {
            if(selected != handle)
                m_asset_assignment =
                    AssetAssignment{target, {selected, m_history.generation(), type}};
        };
        if(ImGui::BeginCombo(property.display_name.c_str(), preview.c_str())) {
            if(ImGui::Selectable("None", !handle))
                request({});
            for(const auto& asset : m_asset_database.get_assets()) {
                if(asset.type == type
                    && ImGui::Selectable(
                        asset.path.generic_string().c_str(), asset.handle == handle))
                    request(asset.handle);
            }
            ImGui::EndCombo();
        }
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

        if(m_loaded_asset != handle) {
            load_asset(*record);
        }

        ImGui::Text("Path: %s", record->path.generic_string().c_str());
        ImGui::Text(
            "Handle: %llu", static_cast<unsigned long long>(record->handle.value()));
        ImGui::Text("Type: %s", Comet::to_string(record->type).data());
        ImGui::Separator();

        if(!m_asset_error.empty()) {
            ImGui::TextColored(
                ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "%s", m_asset_error.c_str());
        }

        if(record->type == Comet::AssetType::Material) {
            if(m_material_data) {
                render_material(*record);
            } else if(ImGui::Button("Retry Load")) {
                load_asset(*record);
            }
            return;
        }

        if(record->type == Comet::AssetType::Texture) {
            if(m_texture_import_settings) {
                render_texture(*record);
            } else if(ImGui::Button("Retry Load")) {
                load_asset(*record);
            }
            return;
        }

        ImGui::TextDisabled("No inspector is available for this asset type");
    }

    void InspectorPanel::render_texture(const Comet::AssetRecord& record) {
        std::optional<Comet::TextureImportSettings> previous_settings;
        const char* color_space =
            texture_color_space_label(m_texture_import_settings->color_space);
        if(ImGui::BeginCombo("Color Space", color_space)) {
            constexpr std::array color_spaces{
                Comet::TextureColorSpace::Srgb, Comet::TextureColorSpace::Linear};
            for(const Comet::TextureColorSpace candidate : color_spaces) {
                const bool selected = candidate == m_texture_import_settings->color_space;
                const char* label = texture_color_space_label(candidate);
                if(ImGui::Selectable(label, selected) && !selected) {
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
        if(ImGui::Checkbox("Flip Y", &flip_y)) {
            if(!previous_settings) {
                previous_settings = *m_texture_import_settings;
            }
            m_texture_import_settings->flip_y = flip_y;
        }

        if(previous_settings) {
            reimport_texture(record, *previous_settings);
        }
    }

    void InspectorPanel::set_material_layout(
        std::shared_ptr<const Comet::MaterialLayout> layout) {
        if(layout)
            m_material_layouts.insert_or_assign(layout->get_name(), std::move(layout));
    }

    std::shared_ptr<const Comet::MaterialLayout> InspectorPanel::find_material_layout(
        const std::string& name) const {
        const auto found = m_material_layouts.find(name);
        if(found != m_material_layouts.end())
            return found->second;
        return Comet::MaterialLayout::find_builtin(name);
    }

    void InspectorPanel::render_material(const Comet::AssetRecord& record) {
        std::optional<Comet::MaterialData> previous_data;
        ImGui::Text("Template: %s", m_material_data->template_name.c_str());
        ImGui::TextDisabled("Template editing is not available yet");
        const auto layout = find_material_layout(m_material_data->template_name);
        if(!layout) {
            ImGui::TextDisabled("No registered layout for this material");
            return;
        }
        const auto remember_previous = [&] {
            if(!previous_data)
                previous_data = *m_material_data;
        };
        if(!layout->get_textures().empty())
            ImGui::SeparatorText("Textures");

        for(const auto& property : layout->get_textures()) {
            const auto& property_name = property.name;
            const auto found = m_material_data->texture_properties.find(property_name);
            Comet::AssetHandle texture_handle;
            if(found != m_material_data->texture_properties.end())
                texture_handle = found->second;
            const std::string preview = texture_preview(m_asset_database, texture_handle);
            ImGui::PushID(property_name.c_str());
            const auto& label =
                property.display_name.empty() ? property.name : property.display_name;
            const auto assign = [&](Comet::AssetHandle value) {
                if(value == texture_handle)
                    return;
                remember_previous();
                m_material_data->scalar_properties.erase(property_name);
                m_material_data->vector_properties.erase(property_name);
                m_material_data->texture_properties[property_name] = value;
                texture_handle = value;
            };
            if(ImGui::BeginCombo(label.c_str(), preview.c_str())) {
                for(const Comet::AssetRecord& candidate : m_texture_assets) {
                    const bool selected = candidate.handle == texture_handle;
                    const std::string label = candidate.path.generic_string();
                    if(ImGui::Selectable(label.c_str(), selected) && !selected) {
                        assign(candidate.handle);
                    }
                    if(selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if(const auto asset = accept_asset_drop(Comet::AssetType::Texture);
                asset && asset->handle != texture_handle) {
                assign(asset->handle);
            }
            ImGui::PopID();
        }

        if(!layout->get_scalars().empty() || !layout->get_vectors().empty())
            ImGui::SeparatorText("Parameters");
        for(const auto& property : layout->get_scalars()) {
            const auto found = m_material_data->scalar_properties.find(property.name);
            float value = property.default_value;
            if(found != m_material_data->scalar_properties.end())
                value = found->second;
            const float before = value;
            const auto& label =
                property.display_name.empty() ? property.name : property.display_name;
            ImGui::PushID(property.name.c_str());
            if(ImGui::DragFloat(label.c_str(), &value, property.step, property.min_value,
                   property.max_value, "%.3f", ImGuiSliderFlags_AlwaysClamp)
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
            const auto& label =
                property.display_name.empty() ? property.name : property.display_name;
            ImGui::PushID(property.name.c_str());
            bool changed = false;
            if(property.semantic
                == Comet::MaterialLayout::VectorProperty::Semantic::Color) {
                changed = ImGui::ColorEdit4(
                    label.c_str(), value.data(), ImGuiColorEditFlags_Float);
            } else {
                changed = ImGui::DragFloat4(label.c_str(), value.data(), 0.01f);
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
            ImGui::TextColored(
                ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "%s", validation_error.c_str());
        }

        if(previous_data && validation_error.empty()) {
            update_material(record, *previous_data);
        }
    }

    void InspectorPanel::load_asset(const Comet::AssetRecord& record) {
        m_loaded_asset = record.handle;
        m_texture_import_settings.reset();
        m_material_data.reset();
        m_texture_assets.clear();
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

        try {
            m_material_data =
                Comet::MaterialSerializer{}.load(m_assets_root / record.path);
            for(const Comet::AssetRecord& asset : m_asset_database.get_assets()) {
                if(asset.type == Comet::AssetType::Texture) {
                    m_texture_assets.push_back(asset);
                }
            }
        } catch(const std::exception& error) {
            m_asset_error = error.what();
        }
    }

    void InspectorPanel::reimport_texture(const Comet::AssetRecord& record,
        const Comet::TextureImportSettings& previous_settings) {
        if(!m_texture_import_settings || !m_reimport_texture_callback) {
            return;
        }

        if(!m_reimport_texture_callback(record.handle, *m_texture_import_settings)) {
            m_texture_import_settings = previous_settings;
            return;
        }
    }

    void InspectorPanel::update_material(
        const Comet::AssetRecord& record, const Comet::MaterialData& previous_data) {
        if(!m_material_data) {
            return;
        }

        if(!m_update_material_callback
            || !m_update_material_callback(record.handle, *m_material_data)) {
            m_material_data = previous_data;
            return;
        }
    }

    std::string InspectorPanel::validate_material() const {
        if(!m_material_data) {
            return "Material data is not loaded";
        }

        const auto layout = find_material_layout(m_material_data->template_name);
        if(!layout)
            return "Material layout is not registered";
        const auto unknown_property = [](const auto& values, const auto& properties) {
            for(const auto& [name, value] : values) {
                if(!std::ranges::any_of(properties,
                       [&](const auto& property) { return property.name == name; }))
                    return name;
            }
            return std::string{};
        };
        for(const auto& name : {unknown_property(m_material_data->texture_properties,
                                    layout->get_textures()),
                unknown_property(
                    m_material_data->scalar_properties, layout->get_scalars()),
                unknown_property(
                    m_material_data->vector_properties, layout->get_vectors())}) {
            if(!name.empty())
                return "Unknown or incorrectly typed property '" + name
                       + "' in this layout";
        }
        for(const auto& property : layout->get_textures()) {
            if(!m_material_data->texture_properties.contains(property.name)) {
                return "Complete texture slot '" + property.name + "' to publish changes";
            }
        }

        for(const auto& [property_name, texture_handle] :
            m_material_data->texture_properties) {
            const Comet::AssetRecord* texture = m_asset_database.find(texture_handle);
            if(!texture) {
                return "Texture property '" + property_name
                       + "' references a missing asset";
            }
            if(texture->type != Comet::AssetType::Texture) {
                return "Texture property '" + property_name
                       + "' references a non-texture asset";
            }
        }
        return {};
    }
}
