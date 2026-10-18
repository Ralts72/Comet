#include "inspector/property_editor_registry.h"
#include "ui/widgets.h"
#include "assets/asset_reference.h"
#include "scene/entity_reference.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
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

        PropertyEditResult edit_enum_property(
            const Comet::PropertyDescriptor& property, void* value) {
            auto& selected = *static_cast<std::string*>(value);
            const char* preview = selected.c_str();
            for(const auto& option : property.enum_options)
                if(option.id == selected)
                    preview = option.display_name.c_str();
            const bool mixed =
                ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_MixedValue;
            if(mixed)
                preview = "多值";
            bool changed = false;
            if(ImGui::BeginCombo(property.display_name.c_str(), preview)) {
                for(const auto& option : property.enum_options) {
                    ImGui::PushID(option.id.c_str());
                    if(ImGui::Selectable(
                           option.display_name.c_str(), !mixed && option.id == selected)
                        && (mixed || option.id != selected)) {
                        selected = option.id;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return PropertyEditResult{.changed = changed, .finished = changed};
        }

        PropertyEditResult edit_integer_property(
            const Comet::PropertyDescriptor& property, void* value) {
            auto& selected = *static_cast<uint32_t*>(value);
            if(!property.bitmask)
                return PropertyEditResult::from_item(ImGui::InputScalar(
                    property.display_name.c_str(), ImGuiDataType_U32, &selected));
            char preview[16];
            std::snprintf(preview, sizeof(preview), "0x%08X", static_cast<unsigned>(selected));
            const bool mixed =
                ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_MixedValue;
            bool changed = false;
            if(ImGui::BeginCombo(property.display_name.c_str(), mixed ? "多值" : preview)) {
                if(mixed)
                    ImGui::TextWrapped("显示主目标的掩码，修改后统一应用到所有选中对象。");
                ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, false);
                if(ImGui::Button("全部") && (mixed || selected != UINT32_MAX)) {
                    selected = UINT32_MAX;
                    changed = true;
                }
                ImGui::SameLine();
                if(ImGui::Button("清空") && (mixed || selected != 0)) {
                    selected = 0;
                    changed = true;
                }
                if(ImGui::BeginTable("layers", 4)) {
                    for(unsigned bit = 0; bit < 32; ++bit) {
                        ImGui::TableNextColumn();
                        changed |= ImGui::CheckboxFlags(
                            std::to_string(bit + 1).c_str(), &selected, uint32_t{1} << bit);
                    }
                    ImGui::EndTable();
                }
                ImGui::PopItemFlag();
                ImGui::EndCombo();
            }
            return {.changed = changed, .finished = changed};
        }

        PropertyEditResult edit_collection(const PropertyEditorRegistry& registry,
            const Comet::PropertyDescriptor& property, Comet::PropertyCollection& collection,
            Comet::Scene* scene, std::optional<std::uint64_t> drop_generation) {
            PropertyEditResult result;
            const auto merge = [&](PropertyEditResult item) {
                result.changed |= item.changed;
                result.active |= item.active;
                result.began |= item.began;
                result.finished |= item.finished;
                if(item.active_item)
                    result.active_item = item.active_item;
            };
            const auto edit = [&](const Comet::PropertyDescriptor& child,
                                  Comet::PropertyValue& value) {
                if(child.read_only || !child.editable) {
                    ImGui::TextDisabled("%s（只读）", child.display_name.c_str());
                    return;
                }
                if(child.type == Comet::PropertyType::EntityReference && scene) {
                    auto& reference = std::get<Comet::EntityUuid>(value);
                    const bool changed = edit_entity_reference(
                        child.display_name.c_str(), reference, *scene, drop_generation);
                    merge({.changed = changed, .finished = changed});
                } else
                    std::visit(
                        [&](auto& item) {
                            merge(registry.edit_property(child, &item, scene, drop_generation));
                        },
                        value);
                if(!child.description.empty() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", child.description.c_str());
            };
            if(!ImGui::TreeNode(property.display_name.c_str()))
                return result;
            auto& data = collection.data();
            if(property.type == Comet::PropertyType::Object) {
                for(auto& [name, value] : data.fields) {
                    const auto child =
                        std::ranges::find(property.members, name, &Comet::PropertyDescriptor::id);
                    if(child == property.members.end())
                        continue;
                    ImGui::PushID(name.c_str());
                    edit(*child, value);
                    ImGui::PopID();
                }
            } else if(property.type == Comet::PropertyType::Optional) {
                bool present = !data.elements.empty();
                if(ImGui::Checkbox("启用值", &present)) {
                    if(present)
                        data.elements.push_back(property.element->default_value());
                    else
                        data.elements.clear();
                    merge({.changed = true, .finished = true});
                }
                if(present)
                    edit(*property.element, data.elements.front());
            } else {
                std::optional<std::size_t> remove;
                for(std::size_t index = 0; index < data.elements.size(); ++index) {
                    ImGui::PushID(int(index));
                    auto child = *property.element;
                    child.display_name = "[" + std::to_string(index) + "]";
                    edit(child, data.elements[index]);
                    if(!property.fixed_size) {
                        ImGui::SameLine();
                        if(ImGui::SmallButton("删除"))
                            remove = index;
                    }
                    ImGui::PopID();
                }
                if(remove) {
                    data.elements.erase(data.elements.begin() + *remove);
                    merge({.changed = true, .finished = true});
                }
                if(!property.fixed_size && data.elements.size() < property.max_elements
                    && ImGui::SmallButton("添加")) {
                    data.elements.push_back(property.element->default_value());
                    merge({.changed = true, .finished = true});
                }
            }
            ImGui::TreePop();
            return result;
        }

        template<typename T>
        PropertyEditResult edit_parameter_value(const PropertyEditorRegistry& registry,
            const std::string& name, const Comet::Script::Property& property, T& value,
            Comet::Scene& scene, const std::optional<std::uint64_t> drop_generation, bool mixed) {
            const auto label = mixed ? name + "（多值）###" + name : name;
            Comet::PropertyDescriptor descriptor{.id = name, .display_name = label};
            if constexpr(std::is_same_v<T, bool>)
                descriptor.type = Comet::PropertyType::Bool;
            else if constexpr(std::is_same_v<T, float>)
                descriptor.type = Comet::PropertyType::Float;
            else if constexpr(std::is_same_v<T, Comet::Math::Vec3>)
                descriptor.type = Comet::PropertyType::Vec3;
            else if constexpr(std::is_same_v<T, Comet::Math::Vec4>) {
                if(property.semantic == Comet::Script::Property::Semantic::Color)
                    return PropertyEditResult::from_item(ImGui::ColorEdit4(label.c_str(), &value.x,
                        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR));
                descriptor.type = Comet::PropertyType::Vec4;
            } else if constexpr(std::is_same_v<T, Comet::EntityUuid>) {
                const bool changed =
                    edit_entity_reference(label.c_str(), value, scene, drop_generation);
                return PropertyEditResult{.changed = changed, .finished = changed};
            } else {
                if(property.semantic == Comet::Script::Property::Semantic::Prefab
                    || property.semantic == Comet::Script::Property::Semantic::Data) {
                    descriptor.type = Comet::PropertyType::AssetHandle;
                    descriptor.asset_type =
                        property.semantic == Comet::Script::Property::Semantic::Prefab
                            ? Comet::AssetType::Prefab
                            : Comet::AssetType::Data;
                    auto handle = Comet::Script::asset_handle(value).value_or(Comet::AssetHandle{});
                    const auto edited = registry.edit_property(descriptor, &handle);
                    if(edited.changed)
                        value = handle ? std::to_string(handle.value()) : std::string{};
                    return edited;
                }
                descriptor.type = Comet::PropertyType::String;
            }
            return registry.edit_property(descriptor, &value);
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
        registry.set_collection_editor(edit_collection);
        const auto register_editor = [&registry](const Comet::PropertyType type,
                                         PropertyEditorRegistry::PropertyEditor editor) {
            if(!registry.register_editor(type, std::move(editor))) {
                LOG_FATAL("Invalid built-in property editor");
            }
        };

        register_editor(
            Comet::PropertyType::Bool, [](const Comet::PropertyDescriptor& property, void* value) {
                return PropertyEditResult::from_item(
                    ImGui::Checkbox(property.display_name.c_str(), static_cast<bool*>(value)));
            });
        register_editor(Comet::PropertyType::UInt32, edit_integer_property);
        register_editor(Comet::PropertyType::Int32, [](const auto& property, void* value) {
            return PropertyEditResult::from_item(
                ImGui::InputScalar(property.display_name.c_str(), ImGuiDataType_S32, value));
        });
        register_editor(
            Comet::PropertyType::Float, [](const Comet::PropertyDescriptor& property, void* value) {
                const float available = ImGui::GetContentRegionAvail().x;
                const float label_width =
                    ImGui::CalcTextSize(property.display_name.c_str(), nullptr, true).x
                    + ImGui::GetStyle().ItemInnerSpacing.x;
                const float width =
                    std::min({available * 0.4f, available - label_width, ImGui::GetFontSize() * 9});
                ImGui::SetNextItemWidth(std::max(1.0f, width));
                return PropertyEditResult::from_item(
                    ImGui::DragFloat(property.display_name.c_str(), static_cast<float*>(value),
                        property.numeric.speed, minimum(property), maximum(property)));
            });
        register_editor(Comet::PropertyType::Vec3, [](const Comet::PropertyDescriptor& property,
                                                       void* value) {
            auto& vector = *static_cast<Comet::Math::Vec3*>(value);
            return PropertyEditResult::from_item(ImGui::DragFloat3(property.display_name.c_str(),
                &vector.x, property.numeric.speed, minimum(property), maximum(property)));
        });
        register_editor(Comet::PropertyType::Vec4, [](const Comet::PropertyDescriptor& property,
                                                       void* value) {
            auto& vector = *static_cast<Comet::Math::Vec4*>(value);
            return PropertyEditResult::from_item(ImGui::DragFloat4(property.display_name.c_str(),
                &vector.x, property.numeric.speed, minimum(property), maximum(property)));
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
                return PropertyEditResult::from_item(
                    Ui::input_text(property.display_name.c_str(), text));
            });

        register_editor(Comet::PropertyType::Enum, edit_enum_property);
        return registry;
    }

    ParameterEditResult PropertyEditorRegistry::edit_parameters(
        const Comet::Script::PropertyMap& properties, Comet::ParameterMap& overrides,
        Comet::Scene& scene, const std::optional<std::uint64_t> drop_generation,
        std::span<const Comet::ParameterMap* const> peers) const {
        ParameterEditResult result;
        for(const auto& [name, property] : properties) {
            auto value = property.default_value;
            if(const auto found = overrides.find(name); found != overrides.end())
                value = found->second;
            const bool mixed = std::ranges::any_of(peers, [&](const auto* parameters) {
                const auto found = parameters->find(name);
                return (found == parameters->end() ? property.default_value : found->second)
                       != value;
            });
            const bool overridden =
                overrides.contains(name) || std::ranges::any_of(peers, [&](const auto* parameters) {
                    return parameters->contains(name);
                });
            ImGui::PushID(name.c_str());
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
            ImGui::BeginGroup();
            auto item = std::visit(
                [&](auto& scalar) {
                    return edit_parameter_value(
                        *this, name, property, scalar, scene, drop_generation, mixed);
                },
                value);
            ImGui::EndGroup();
            ImGui::PopItemFlag();
            if(item.changed)
                overrides.insert_or_assign(name, std::move(value));
            if(ImGui::BeginPopupContextItem("Parameter actions",
                   ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverExistingPopup)) {
                if(ImGui::MenuItem(
                       "使用脚本默认值###Use script default", nullptr, false, overridden)) {
                    overrides.erase(name);
                    item = {.changed = true, .finished = true};
                }
                ImGui::EndPopup();
            }
            if(item.changed)
                result.changed_parameters.push_back(name);
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
    }
}
