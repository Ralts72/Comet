#include "inspector/property_editor_registry.h"
#include "assets/asset_reference.h"

#include <imgui.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace CometEditor {
    namespace {
        float minimum(const Comet::PropertyDescriptor& property) {
            if(!property.numeric.minimum && !property.numeric.maximum) {
                return 0.0f;
            }
            return property.numeric.minimum.value_or(-std::numeric_limits<float>::max());
        }

        float maximum(const Comet::PropertyDescriptor& property) {
            if(!property.numeric.minimum && !property.numeric.maximum) {
                return 0.0f;
            }
            return property.numeric.maximum.value_or(std::numeric_limits<float>::max());
        }
    }

    PropertyEditResult PropertyEditResult::from_item(const bool changed) {
        return {.changed = changed,
            .active = ImGui::IsItemActive(),
            .began = ImGui::IsItemActivated(),
            .finished = ImGui::IsItemDeactivated()};
    }

    PropertyEditorRegistry create_property_editor_registry(
        const Comet::AssetDatabase& database) {
        PropertyEditorRegistry registry;
        const auto register_editor = [&registry](const Comet::PropertyType type,
                                         PropertyEditorRegistry::PropertyEditor editor) {
            if(!registry.register_editor(type, std::move(editor))) {
                throw std::logic_error("Invalid built-in property editor");
            }
        };

        register_editor(Comet::PropertyType::Bool,
            [](const Comet::PropertyDescriptor& property, void* value) {
                return PropertyEditResult::from_item(ImGui::Checkbox(
                    property.display_name.c_str(), static_cast<bool*>(value)));
            });
        register_editor(Comet::PropertyType::Float,
            [](const Comet::PropertyDescriptor& property, void* value) {
                const float available = ImGui::GetContentRegionAvail().x;
                const float label_width =
                    ImGui::CalcTextSize(property.display_name.c_str()).x
                    + ImGui::GetStyle().ItemInnerSpacing.x;
                const float width = std::min({available * 0.4f, available - label_width,
                    ImGui::GetFontSize() * 9});
                ImGui::SetNextItemWidth(std::max(1.0f, width));
                return PropertyEditResult::from_item(ImGui::DragFloat(
                    property.display_name.c_str(), static_cast<float*>(value),
                    property.numeric.speed, minimum(property), maximum(property)));
            });
        register_editor(Comet::PropertyType::Vec3,
            [](const Comet::PropertyDescriptor& property, void* value) {
                auto& vector = *static_cast<Comet::Math::Vec3*>(value);
                return PropertyEditResult::from_item(
                    ImGui::DragFloat3(property.display_name.c_str(), &vector.x,
                        property.numeric.speed, minimum(property), maximum(property)));
            });
        register_editor(Comet::PropertyType::AssetHandle,
            [&database](const Comet::PropertyDescriptor& property, void* value) {
                auto& handle = *static_cast<Comet::AssetHandle*>(value);
                const bool changed = edit_asset_reference(
                    property.display_name.c_str(), handle, database, property.asset_type);
                // 下拉选择是离散提交，不把弹窗内部的最后一个 Item 当作编辑手势。
                return PropertyEditResult{.changed = changed, .finished = changed};
            });

        register_editor(Comet::PropertyType::String,
            [](const Comet::PropertyDescriptor& property, void* value) {
                auto& text = *static_cast<std::string*>(value);
                return PropertyEditResult::from_item(ImGui::InputText(
                    property.display_name.c_str(), text.data(), text.capacity() + 1,
                    ImGuiInputTextFlags_CallbackResize,
                    [](ImGuiInputTextCallbackData* data) {
                        auto& text = *static_cast<std::string*>(data->UserData);
                        text.resize(static_cast<std::size_t>(data->BufTextLen));
                        data->Buf = text.data();
                        return 0;
                    },
                    &text));
            });

        return registry;
    }
}
