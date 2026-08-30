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
        ReloadMaterialCallback reload_material_callback)
        : EditorPanel("Inspector"),
          m_selection(selection),
          m_component_registry(component_registry),
          m_property_editor_registry(property_editor_registry),
          m_asset_database(asset_database),
          m_assets_root(std::move(assets_root)),
          m_reload_material_callback(std::move(reload_material_callback)) {}

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

    void InspectorPanel::render_entity(Comet::Entity entity) const {
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
                    static_cast<void>(m_property_editor_registry.edit_property(
                        property, property.get_value(component)));
                    ImGui::PopID();
                }
            }
            ImGui::PopID();
        }
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
        if(!m_asset_status.empty()) {
            ImGui::TextDisabled("%s", m_asset_status.c_str());
        }

        if(record->type == Comet::AssetType::Material) {
            if(m_material_data) {
                render_material(*record);
            } else if(ImGui::Button("Retry Load")) {
                load_asset(*record);
            }
            return;
        }

        ImGui::TextDisabled("No inspector is available for this asset type");
    }

    void InspectorPanel::render_material(const Comet::AssetRecord& record) {
        ImGui::Text("Template: %s", m_material_data->template_name.c_str());
        ImGui::TextDisabled("Template editing is not available yet");
        ImGui::SeparatorText("Texture Properties");

        const std::vector<Comet::AssetRecord> assets =
                m_asset_database.get_assets();
        for(auto& [property_name, texture_handle]:
            m_material_data->texture_properties) {
            const std::string preview = texture_preview(
                m_asset_database, texture_handle);
            ImGui::PushID(property_name.c_str());
            if(ImGui::BeginCombo(property_name.c_str(), preview.c_str())) {
                for(const Comet::AssetRecord& candidate: assets) {
                    if(candidate.type != Comet::AssetType::Texture) {
                        continue;
                    }

                    const bool selected = candidate.handle == texture_handle;
                    const std::string label = candidate.path.generic_string();
                    if(ImGui::Selectable(label.c_str(), selected)) {
                        texture_handle = candidate.handle;
                        m_material_dirty = true;
                        m_asset_error.clear();
                        m_asset_status.clear();
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
        } else if(m_material_dirty) {
            ImGui::TextDisabled("Unsaved changes");
        }

        ImGui::BeginDisabled(!validation_error.empty());
        if(ImGui::Button("Save")) {
            save_material(record);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("Revert")) {
            load_asset(record);
        }
    }

    void InspectorPanel::load_asset(const Comet::AssetRecord& record) {
        m_loaded_asset = record.handle;
        m_material_data.reset();
        m_material_dirty = false;
        m_asset_error.clear();
        m_asset_status.clear();

        if(record.type != Comet::AssetType::Material) {
            return;
        }

        try {
            m_material_data = Comet::MaterialSerializer{}.load(
                m_assets_root / record.path);
        } catch(const std::exception& error) {
            m_asset_error = error.what();
        }
    }

    void InspectorPanel::save_material(const Comet::AssetRecord& record) {
        if(!m_material_data) {
            return;
        }

        try {
            Comet::MaterialSerializer{}.save(
                *m_material_data,
                m_assets_root / record.path);
            m_material_dirty = false;
            if(m_reload_material_callback
               && !m_reload_material_callback(record.handle)) {
                m_asset_error =
                        "Material was saved, but runtime reload failed; the previous runtime material is still active";
                m_asset_status.clear();
                return;
            }
            m_asset_error.clear();
            m_asset_status = "Material saved and reloaded";
        } catch(const std::exception& error) {
            m_asset_error = error.what();
            m_asset_status.clear();
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
