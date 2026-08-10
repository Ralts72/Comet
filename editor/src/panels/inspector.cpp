#include "inspector.h"
#include "property_editor_registry.h"
#include "selection.h"

#include "scene/component_registry.h"

#include <algorithm>
#include <array>
#include <imgui.h>

namespace CometEditor {
    namespace {
        constexpr std::size_t ENTITY_NAME_CAPACITY = 256;
    }

    InspectorPanel::InspectorPanel(
        SelectionService& selection,
        const Comet::ComponentRegistry& component_registry,
        const PropertyEditorRegistry& property_editor_registry)
        : EditorPanel("Inspector"),
          m_selection(selection),
          m_component_registry(component_registry),
          m_property_editor_registry(property_editor_registry) {}

    void InspectorPanel::render() {
        if(!m_user_visible) return;

        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }

        Comet::Entity entity = m_selection.get_selected_entity();
        if(!entity) {
            ImGui::TextUnformatted("No entity selected");
            ImGui::End();
            return;
        }

        auto& name = entity.get_component<Comet::NameComponent>().name;
        std::array<char, ENTITY_NAME_CAPACITY> name_buffer{};
        std::copy_n(name.data(), std::min(name.size(), name_buffer.size() - 1), name_buffer.data());

        ImGui::Text("Entity ID: %llu", static_cast<unsigned long long>(entity.get_id()));
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

        ImGui::End();
    }
}
