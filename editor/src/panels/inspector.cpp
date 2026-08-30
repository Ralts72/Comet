#include "inspector.h"
#include "property_editor_registry.h"
#include "selection.h"

#include "asset/serialization/material_serializer.h"
#include "scene/component_registry.h"

#include <algorithm>
#include <array>
#include <exception>
#include <imgui.h>
#include <string>
#include <utility>
#include <vector>

namespace CometEditor {
    namespace {
        constexpr std::size_t ENTITY_NAME_CAPACITY = 256;

        std::string texture_preview(
            const Comet::AssetDatabase& database,
            const Comet::AssetHandle handle) {
            const Comet::AssetRecord* record = database.find(handle);
            if(!record) {
                return "Missing texture (" + std::to_string(handle.value()) + ")";
            }
            if(record->type != Comet::AssetType::Texture) {
                return "Invalid asset type (" + std::to_string(handle.value()) + ")";
            }
            return record->path.generic_string();
        }
    }

    InspectorPanel::InspectorPanel(
        SelectionService& selection,
        const Comet::ComponentRegistry& component_registry,
        const PropertyEditorRegistry& property_editor_registry,
        const Comet::AssetDatabase& asset_database,
        std::filesystem::path assets_root,
        UpdateMaterialCallback update_material_callback,
        ReimportTextureCallback reimport_texture_callback)
        : EditorPanel("Inspector"),
          m_selection(selection),
          m_component_registry(component_registry),
          m_property_editor_registry(property_editor_registry),
          m_asset_database(asset_database),
          m_assets_root(std::move(assets_root)),
          m_update_material_callback(std::move(update_material_callback)),
          m_reimport_texture_callback(std::move(reimport_texture_callback)) {}

    void InspectorPanel::render() {
        if(!m_user_visible) return;

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }

        if(Comet::Entity entity = m_selection.get_selected_entity()) {
            render_entity(entity);
        } else if(const Comet::AssetHandle asset =
                      m_selection.get_selected_asset()) {
            render_asset(asset);
        } else {
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
        auto& name = entity.get_component<Comet::NameComponent>().name;
        std::array<char, ENTITY_NAME_CAPACITY> name_buffer{};
        std::copy_n(
            name.data(),
            std::min(name.size(), name_buffer.size() - 1),
            name_buffer.data());

        ImGui::Text(
            "Entity ID: %llu",
            static_cast<unsigned long long>(entity.get_id()));
        if(ImGui::InputText("Name", name_buffer.data(), name_buffer.size())) {
            name = name_buffer.data();
        }

        ImGui::Separator();

        for(const Comet::ComponentDescriptor& component_descriptor:
            m_component_registry.components()) {
            if(!component_descriptor.has_component(entity)) {
                continue;
            }

            ImGui::PushID(component_descriptor.id.c_str());
            if(ImGui::CollapsingHeader(
                   component_descriptor.display_name.c_str(),
                   ImGuiTreeNodeFlags_DefaultOpen)) {
                void* component = component_descriptor.get_component(entity);
                for(const Comet::PropertyDescriptor& property:
                    component_descriptor.properties) {
                    ImGui::PushID(property.id.c_str());
                    render_entity_property(
                        entity,
                        component_descriptor,
                        component,
                        property);
                    ImGui::PopID();
                }
            }
            ImGui::PopID();
        }
    }

    void InspectorPanel::render_entity_property(
        const Comet::Entity entity,
        const Comet::ComponentDescriptor& component_descriptor,
        void* component,
        const Comet::PropertyDescriptor& property) {
        const std::optional<Comet::PropertyValue> before =
            property.copy_value(component);
        static_cast<void>(m_property_editor_registry.edit_property(
            property, property.get_value(component)));

        if(m_entity_property_edit_callback
           && before
           && ImGui::IsItemActivated()) {
            m_active_property_edit = ActivePropertyEdit{
                .entity_uuid = entity.get_uuid(),
                .component_id = component_descriptor.id,
                .property_id = property.id,
                .before = *before
            };
        }

        if(!ImGui::IsItemDeactivated() || !m_active_property_edit) {
            return;
        }
        const bool matches_active_property =
            m_active_property_edit->entity_uuid == entity.get_uuid()
            && m_active_property_edit->component_id
                == component_descriptor.id
            && m_active_property_edit->property_id == property.id;
        if(!matches_active_property) {
            return;
        }

        if(ImGui::IsItemDeactivatedAfterEdit()) {
            const std::optional<Comet::PropertyValue> after =
                property.copy_value(component);
            if(after
               && !Comet::property_values_equal(
                   m_active_property_edit->before, *after)) {
                m_entity_property_edit_callback(
                    m_active_property_edit->entity_uuid,
                    m_active_property_edit->component_id,
                    m_active_property_edit->property_id,
                    m_active_property_edit->before,
                    *after);
            }
        }
        m_active_property_edit.reset();
    }

    void InspectorPanel::render_asset(const Comet::AssetHandle handle) {
        const Comet::AssetRecord* record = m_asset_database.find(handle);
        if(!record) {
            ImGui::TextColored(
                ImVec4(0.9f, 0.25f, 0.2f, 1.0f),
                "Selected asset is no longer indexed");
            return;
        }

        if(m_loaded_asset != handle) {
            load_asset(*record);
        }

        ImGui::Text("Path: %s", record->path.generic_string().c_str());
        ImGui::Text(
            "Handle: %llu",
            static_cast<unsigned long long>(record->handle.value()));
        ImGui::Text("Type: %s", Comet::to_string(record->type).data());
        ImGui::Separator();

        if(!m_asset_error.empty()) {
            ImGui::TextColored(
                ImVec4(0.9f, 0.25f, 0.2f, 1.0f),
                "%s",
                m_asset_error.c_str());
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
            m_texture_import_settings->color_space
                == Comet::TextureColorSpace::Srgb
            ? "sRGB"
            : "Linear";
        if(ImGui::BeginCombo("Color Space", color_space)) {
            constexpr std::array color_spaces{
                Comet::TextureColorSpace::Srgb,
                Comet::TextureColorSpace::Linear
            };
            for(const Comet::TextureColorSpace candidate: color_spaces) {
                const bool selected =
                    candidate == m_texture_import_settings->color_space;
                const char* label = candidate == Comet::TextureColorSpace::Srgb
                    ? "sRGB"
                    : "Linear";
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

        for(auto& [property_name, texture_handle]:
            m_material_data->texture_properties) {
            const std::string preview = texture_preview(
                m_asset_database, texture_handle);
            ImGui::PushID(property_name.c_str());
            if(ImGui::BeginCombo(property_name.c_str(), preview.c_str())) {
                for(const Comet::AssetRecord& candidate: m_texture_assets) {
                    const bool selected = candidate.handle == texture_handle;
                    const std::string label = candidate.path.generic_string();
                    if(ImGui::Selectable(label.c_str(), selected) && !selected) {
                        previous_data = *m_material_data;
                        texture_handle = candidate.handle;
                    }
                    if(selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }

        const std::string validation_error = validate_material();
        if(!validation_error.empty()) {
            ImGui::TextColored(
                ImVec4(0.9f, 0.25f, 0.2f, 1.0f),
                "%s",
                validation_error.c_str());
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
        m_texture_assets.clear();
        m_asset_error.clear();

        if(record.type == Comet::AssetType::Texture) {
            const auto* settings = std::get_if<Comet::TextureImportSettings>(
                &record.import_settings);
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
            m_material_data = Comet::MaterialSerializer{}.load(
                m_assets_root / record.path);
            for(const Comet::AssetRecord& asset: m_asset_database.get_assets()) {
                if(asset.type == Comet::AssetType::Texture) {
                    m_texture_assets.push_back(asset);
                }
            }
        } catch(const std::exception& error) {
            m_asset_error = error.what();
        }
    }

    void InspectorPanel::reimport_texture(
        const Comet::AssetRecord& record,
        const Comet::TextureImportSettings& previous_settings) {
        if(!m_texture_import_settings || !m_reimport_texture_callback) {
            return;
        }

        if(!m_reimport_texture_callback(
               record.handle,
               *m_texture_import_settings)) {
            m_texture_import_settings = previous_settings;
            return;
        }
    }

    void InspectorPanel::update_material(
        const Comet::AssetRecord& record,
        const Comet::MaterialData& previous_data) {
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

        for(const auto& [property_name, texture_handle]:
            m_material_data->texture_properties) {
            const Comet::AssetRecord* texture =
                    m_asset_database.find(texture_handle);
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
