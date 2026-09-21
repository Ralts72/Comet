#include "inspector/property_editor_registry.h"
#include "ui/language.h"
#include "assets/asset_reference.h"

#include <imgui.h>
#include <algorithm>
#include <limits>
#include "diagnostics/logger.h"

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
            .finished = ImGui::IsItemDeactivated(),
            .active_item = ImGui::IsItemActive() ? ImGui::GetItemID() : 0};
    }

    void PropertyEditResult::include_item(bool item_changed) {
        const auto item = from_item(item_changed);
        changed |= item.changed;
        active |= item.active;
        began |= item.began;
        finished |= item.finished;
        if(item.active_item)
            active_item = item.active_item;
    }

    PropertyEditorRegistry create_property_editor_registry(const Comet::AssetDatabase& database) {
        PropertyEditorRegistry registry;
        const auto register_editor = [&registry](const Comet::PropertyType type,
                                         PropertyEditorRegistry::PropertyEditor editor) {
            if(!registry.register_editor(type, std::move(editor))) {
                LOG_FATAL("Invalid built-in property editor");
            }
        };

        register_editor(
            Comet::PropertyType::Bool, [](const Comet::PropertyDescriptor& property, void* value) {
                return PropertyEditResult::from_item(ImGui::Checkbox(
                    Ui::label(property.display_name.c_str()).c_str(), static_cast<bool*>(value)));
            });
        register_editor(Comet::PropertyType::Float, [](const Comet::PropertyDescriptor& property,
                                                        void* value) {
            const float available = ImGui::GetContentRegionAvail().x;
            const float label_width = ImGui::CalcTextSize(Ui::text(property.display_name.c_str())).x
                                      + ImGui::GetStyle().ItemInnerSpacing.x;
            const float width =
                std::min({available * 0.4f, available - label_width, ImGui::GetFontSize() * 9});
            ImGui::SetNextItemWidth(std::max(1.0f, width));
            return PropertyEditResult::from_item(ImGui::DragFloat(
                Ui::label(property.display_name.c_str()).c_str(), static_cast<float*>(value),
                property.numeric.speed, minimum(property), maximum(property)));
        });
        register_editor(
            Comet::PropertyType::Vec3, [](const Comet::PropertyDescriptor& property, void* value) {
                auto& vector = *static_cast<Comet::Math::Vec3*>(value);
                return PropertyEditResult::from_item(
                    ImGui::DragFloat3(Ui::label(property.display_name.c_str()).c_str(), &vector.x,
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
                    Ui::label(property.display_name.c_str()).c_str(), text.data(),
                    text.capacity() + 1, ImGuiInputTextFlags_CallbackResize,
                    [](ImGuiInputTextCallbackData* data) {
                        auto& text = *static_cast<std::string*>(data->UserData);
                        text.resize(static_cast<std::size_t>(data->BufTextLen));
                        data->Buf = text.data();
                        return 0;
                    },
                    &text));
            });

        register_editor(
            Comet::PropertyType::Enum, [](const Comet::PropertyDescriptor& property, void* value) {
                auto& selected = *static_cast<std::string*>(value);
                const char* preview = selected.c_str();
                for(const auto& option : property.enum_options)
                    if(option.id == selected)
                        preview = Ui::text(option.display_name.c_str());
                bool changed = false;
                if(ImGui::BeginCombo(Ui::label(property.display_name.c_str()).c_str(), preview)) {
                    for(const auto& option : property.enum_options) {
                        ImGui::PushID(option.id.c_str());
                        if(ImGui::Selectable(Ui::label(option.display_name.c_str()).c_str(),
                               option.id == selected)
                            && option.id != selected) {
                            selected = option.id;
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                return PropertyEditResult{.changed = changed, .finished = changed};
            });
        register_editor(Comet::PropertyType::Parameters,
            [scalar_editors = registry](const Comet::PropertyDescriptor&, void* value) {
                auto& parameters = *static_cast<Comet::ParameterMap*>(value);
                PropertyEditResult result;
                for(auto& [name, parameter] : parameters) {
                    ImGui::PushID(name.c_str());
                    const auto item = std::visit(
                        [&](auto& scalar) {
                            using T = std::remove_cvref_t<decltype(scalar)>;
                            Comet::PropertyDescriptor descriptor{.id = name, .display_name = name};
                            if constexpr(std::is_same_v<T, bool>)
                                descriptor.type = Comet::PropertyType::Bool;
                            else if constexpr(std::is_same_v<T, float>)
                                descriptor.type = Comet::PropertyType::Float;
                            else if constexpr(std::is_same_v<T, Comet::Math::Vec3>)
                                descriptor.type = Comet::PropertyType::Vec3;
                            else
                                descriptor.type = Comet::PropertyType::String;
                            return scalar_editors.edit_property(descriptor, &scalar);
                        },
                        parameter);
                    result.changed |= item.changed;
                    result.active |= item.active;
                    result.began |= item.began;
                    result.finished |= item.finished;
                    if(item.active_item)
                        result.active_item = item.active_item;
                    ImGui::PopID();
                }
                if(result.active)
                    result.finished = false;
                return result;
            });
        return registry;
    }
}
