#include "property_editor_registry.h"

#include <cstdint>
#include <imgui.h>
#include <limits>
#include <stdexcept>

namespace CometEditor {
    namespace {
        float minimum(const Comet::PropertyDescriptor& property) {
            if(!property.numeric.minimum && !property.numeric.maximum) {
                return 0.0f;
            }
            return property.numeric.minimum.value_or(
                -std::numeric_limits<float>::max());
        }

        float maximum(const Comet::PropertyDescriptor& property) {
            if(!property.numeric.minimum && !property.numeric.maximum) {
                return 0.0f;
            }
            return property.numeric.maximum.value_or(
                std::numeric_limits<float>::max());
        }
    }

    PropertyEditorRegistry create_property_editor_registry() {
        PropertyEditorRegistry registry;
        const auto register_editor = [&registry](
            const Comet::PropertyType type,
            PropertyEditorRegistry::PropertyEditor editor) {
            if(!registry.register_editor(type, std::move(editor))) {
                throw std::logic_error("Invalid built-in property editor");
            }
        };

        register_editor(
            Comet::PropertyType::Bool,
            [](const Comet::PropertyDescriptor& property, void* value) {
                return ImGui::Checkbox(
                    property.display_name.c_str(), static_cast<bool*>(value));
            });
        register_editor(
            Comet::PropertyType::Float,
            [](const Comet::PropertyDescriptor& property, void* value) {
                return ImGui::DragFloat(
                    property.display_name.c_str(),
                    static_cast<float*>(value),
                    property.numeric.speed,
                    minimum(property),
                    maximum(property));
            });
        register_editor(
            Comet::PropertyType::Vec3,
            [](const Comet::PropertyDescriptor& property, void* value) {
                auto& vector = *static_cast<Comet::Math::Vec3*>(value);
                return ImGui::DragFloat3(
                    property.display_name.c_str(),
                    &vector.x,
                    property.numeric.speed,
                    minimum(property),
                    maximum(property));
            });
        register_editor(
            Comet::PropertyType::AssetHandle,
            [](const Comet::PropertyDescriptor& property, void* value) {
                auto& handle = *static_cast<Comet::AssetHandle*>(value);
                std::uint64_t raw_value = handle.value();
                if(!ImGui::InputScalar(
                       property.display_name.c_str(),
                       ImGuiDataType_U64,
                       &raw_value)) {
                    return false;
                }
                handle = Comet::AssetHandle(raw_value);
                return true;
            });

        return registry;
    }
}
