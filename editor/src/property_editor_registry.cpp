#include "property_editor_registry.h"
#include "asset/database.h"

#include <imgui.h>
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

    bool edit_asset_reference(const char* label, Comet::AssetHandle& handle,
        const Comet::AssetDatabase& database, const std::optional<Comet::AssetType> type,
        const bool allow_none) {
        std::string preview = "None";
        if(handle.is_valid()) {
            const auto* record = database.find(handle);
            if(!record) {
                preview = "Missing (" + std::to_string(handle.value()) + ")";
            } else if(type && record->type != *type) {
                preview = "Invalid type: " + record->path.generic_string();
            } else {
                preview = record->path.generic_string();
            }
        }

        bool changed = false;
        if(ImGui::BeginCombo(label, preview.c_str())) {
            if(allow_none && ImGui::Selectable("None", !handle.is_valid())
                && handle.is_valid()) {
                handle = {};
                changed = true;
            }
            bool has_candidates = false;
            for(const auto& candidate : database.get_assets()) {
                if(type && candidate.type != *type) {
                    continue;
                }
                has_candidates = true;
                const bool selected = candidate.handle == handle;
                const std::string item_label = candidate.path.generic_string() + "###"
                                               + std::to_string(candidate.handle.value());
                if(ImGui::Selectable(item_label.c_str(), selected) && !selected) {
                    handle = candidate.handle;
                    changed = true;
                }
                if(selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            if(!has_candidates) {
                ImGui::TextDisabled("No matching assets");
            }
            ImGui::EndCombo();
        }
        return changed;
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
