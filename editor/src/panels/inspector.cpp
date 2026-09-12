#include "inspector.h"
#include "property_editor_registry.h"
#include "selection.h"
#include "scene_commands.h"
#include "diagnostics/logger.h"

#include "asset/serialization/material_serializer.h"
#include "scene/component_registry.h"

#include <array>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <utility>

namespace CometEditor {
    namespace {
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

    InspectorPanel::InspectorPanel(const EditorState& state, SelectionService& selection,
        CommandHistory& history, PropertyEditTransaction& property_edit,
        const Comet::ComponentRegistry& component_registry,
        const PropertyEditorRegistry& property_editor_registry,
        const Comet::AssetDatabase& asset_database, std::filesystem::path assets_root,
        UpdateMaterialCallback update_material_callback,
        ReimportTextureCallback reimport_texture_callback)
        : EditorPanel("Inspector"), m_state(state), m_selection(selection),
          m_history(history), m_property_edit(property_edit),
          m_component_registry(component_registry),
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
        m_asset_error.clear();
    }

    void InspectorPanel::render_entity(Comet::Entity entity) {
        ImGui::Text("Entity ID: %llu", static_cast<unsigned long long>(entity.get_id()));

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
            return;
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
        const PropertyEditResult result = std::visit(
            [&](auto& edited) {
                return m_property_editor_registry.edit_property(property, &edited);
            },
            *value);
        const bool changed = result.changed;
        const bool active = result.active;
        const bool activated = result.began;
        const bool deactivated = result.finished;
        if(m_state.mode == EditorMode::Play) {
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
        if(m_state.mode != EditorMode::Edit || !ImGui::BeginDragDropTarget())
            return std::nullopt;
        std::optional<AssetDragPayload> result;
        const auto* payload = ImGui::GetDragDropPayload();
        if(payload && payload->IsDataType(AssetDragPayload::TYPE)
            && payload->DataSize == sizeof(AssetDragPayload)) {
            const auto asset = *static_cast<const AssetDragPayload*>(payload->Data);
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

    void InspectorPanel::render_asset_property(
        const PropertyEditTransaction::Target& target,
        const Comet::PropertyDescriptor& property, const Comet::AssetHandle handle) {
        auto selected = handle;
        const auto type = *property.asset_type;
        if(edit_asset_reference(
               property.display_name.c_str(), selected, m_asset_database, type))
            m_asset_assignment = AssetAssignment{
                target, {selected, m_asset_database.get_revision(selected),
                            m_history.generation(), type}};
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

    void InspectorPanel::render_material(const Comet::AssetRecord& record) {
        std::optional<Comet::MaterialData> previous_data;
        ImGui::Text("Template: %s", m_material_data->template_name.c_str());
        ImGui::TextDisabled("Template editing is not available yet");
        ImGui::SeparatorText("Texture Properties");

        for(auto& [property_name, texture_handle] : m_material_data->texture_properties) {
            auto selected = texture_handle;
            ImGui::PushID(property_name.c_str());
            if(edit_asset_reference(property_name.c_str(), selected, m_asset_database,
                   Comet::AssetType::Texture, false)) {
                if(!previous_data) {
                    previous_data = *m_material_data;
                }
                texture_handle = selected;
            }
            if(const auto asset = accept_asset_drop(Comet::AssetType::Texture);
                asset && asset->handle != texture_handle) {
                if(!previous_data)
                    previous_data = *m_material_data;
                texture_handle = asset->handle;
            }
            ImGui::PopID();
        }

        const std::string validation_error = validate_material();
        if(!validation_error.empty()) {
            ImGui::TextColored(
                ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "%s", validation_error.c_str());
        }

        if(previous_data) {
            if(validation_error.empty()) {
                update_material(record, *previous_data);
            } else {
                m_material_data = *previous_data;
            }
        }
    }

    void InspectorPanel::load_asset(const Comet::AssetRecord& record) {
        m_loaded_asset = record.handle;
        m_texture_import_settings.reset();
        m_material_data.reset();
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

        auto data = Comet::MaterialSerializer{}.load(m_assets_root / record.path);
        if(data)
            m_material_data = std::move(data).value();
        else
            m_asset_error = data.error();
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
        if(!m_material_data || !m_update_material_callback) {
            return;
        }

        if(!m_update_material_callback(record.handle, *m_material_data)) {
            m_material_data = previous_data;
            return;
        }
    }

    std::string InspectorPanel::validate_material() const {
        if(!m_material_data) {
            return "Material data is not loaded";
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
