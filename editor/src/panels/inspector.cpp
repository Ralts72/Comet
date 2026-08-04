#include "inspector.h"
#include "selection.h"

#include <algorithm>
#include <array>
#include <imgui.h>

namespace CometEditor {
    namespace {
        constexpr std::size_t ENTITY_NAME_CAPACITY = 256;
    }

    InspectorPanel::InspectorPanel(SelectionService& selection)
        : EditorPanel("Inspector"), m_selection(selection) {}

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

        if(ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& transform = entity.get_component<Comet::TransformComponent>();
            ImGui::DragFloat3("Translation", &transform.translation.x, 0.1f);
            ImGui::DragFloat3("Rotation", &transform.rotation.x, 1.0f);
            ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);
        }

        ImGui::End();
    }
}
