#pragma once

#include "asset/metadata.h"
#include "common/export.h"
#include "core/math_utils.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Comet {
    enum class PropertyType { Bool, Float, Vec3, AssetHandle, String, Enum };

    using PropertyValue = std::variant<bool, float, Math::Vec3, AssetHandle, std::string>;

    [[nodiscard]] COMET_API bool property_values_equal(
        const PropertyValue& left, const PropertyValue& right);

    struct NumericPropertyMetadata {
        float speed = 0.1f;
        std::optional<float> minimum;
        std::optional<float> maximum;
    };

    struct PropertyMetadata {
        bool editable = true;
        bool serializable = true;
        bool transient = false;
        bool read_only = false;
        NumericPropertyMetadata numeric;
        std::optional<AssetType> asset_type;
    };

    struct PropertyDescriptor {
        struct EnumOption {
            std::string id;
            std::string display_name;
        };
        [[nodiscard]] COMET_API std::optional<PropertyValue> copy_value(
            const void* component) const;
        [[nodiscard]] COMET_API bool assign_value(
            void* component, const PropertyValue& value) const;

        std::string id;
        std::string display_name;
        PropertyType type = PropertyType::Float;
        bool editable = true;
        bool serializable = true;
        bool transient = false;
        bool read_only = false;
        NumericPropertyMetadata numeric;
        std::optional<AssetType> asset_type;
        std::function<void*(void*)> mutable_accessor;
        std::function<const void*(const void*)> const_accessor;
        std::function<void(void*)> on_changed;
        std::vector<EnumOption> enum_options;
        std::function<std::optional<std::string>(const void*)> read_enum;
        std::function<bool(void*, std::string_view)> write_enum;

        [[nodiscard]] void* get_value(void* component) const {
            if(component == nullptr || !mutable_accessor) {
                return nullptr;
            }
            return mutable_accessor(component);
        }

        [[nodiscard]] const void* get_value(const void* component) const {
            if(component == nullptr || !const_accessor) {
                return nullptr;
            }
            return const_accessor(component);
        }

        void notify_changed(void* value) const {
            if(value != nullptr && on_changed) {
                on_changed(value);
            }
        }
    };

    template<typename Component, typename Value>
    PropertyDescriptor make_property_descriptor(std::string id, std::string display_name,
        Value Component::* member, PropertyMetadata metadata = {}) {
        using PropertyValue = std::remove_cvref_t<Value>;
        static_assert(std::is_same_v<PropertyValue, bool> || std::is_same_v<PropertyValue, float>
                          || std::is_same_v<PropertyValue, Math::Vec3>
                          || std::is_same_v<PropertyValue, AssetHandle>
                          || std::is_same_v<PropertyValue, std::string>,
            "Unsupported property type");

        constexpr PropertyType type = [] {
            if constexpr(std::is_same_v<PropertyValue, bool>) {
                return PropertyType::Bool;
            } else if constexpr(std::is_same_v<PropertyValue, float>) {
                return PropertyType::Float;
            } else if constexpr(std::is_same_v<PropertyValue, Math::Vec3>) {
                return PropertyType::Vec3;
            } else if constexpr(std::is_same_v<PropertyValue, std::string>) {
                return PropertyType::String;
            } else {
                return PropertyType::AssetHandle;
            }
        }();

        return {.id = std::move(id),
            .display_name = std::move(display_name),
            .type = type,
            .editable = metadata.editable,
            .serializable = metadata.serializable,
            .transient = metadata.transient,
            .read_only = metadata.read_only,
            .numeric = std::move(metadata.numeric),
            .asset_type = metadata.asset_type,
            .mutable_accessor = [member](void* component) -> void* {
                return &(static_cast<Component*>(component)->*member);
            },
            .const_accessor = [member](const void* component) -> const void* {
                return &(static_cast<const Component*>(component)->*member);
            }};
    }

    template<typename Component, typename Value, typename Callback>
    PropertyDescriptor make_property_descriptor(std::string id, std::string display_name,
        Value Component::* member, PropertyMetadata metadata, Callback&& on_changed) {
        PropertyDescriptor descriptor = make_property_descriptor(
            std::move(id), std::move(display_name), member, std::move(metadata));
        descriptor.on_changed = [callback = std::forward<Callback>(on_changed)](
                                    void* value) mutable { callback(*static_cast<Value*>(value)); };
        return descriptor;
    }

    // 枚举仍以实际 C++ 类型存储；快照/场景文件使用稳定字符串，不重解释 underlying type。
    template<typename Component, typename Enum>
        requires std::is_enum_v<Enum>
    PropertyDescriptor make_enum_property_descriptor(std::string id, std::string display_name,
        Enum Component::* member,
        std::vector<std::pair<Enum, PropertyDescriptor::EnumOption>> options) {
        PropertyDescriptor descriptor{.id = std::move(id),
            .display_name = std::move(display_name),
            .type = PropertyType::Enum,
            .mutable_accessor = [member](void* component) -> void* {
                return &(static_cast<Component*>(component)->*member);
            },
            .const_accessor = [member](const void* component) -> const void* {
                return &(static_cast<const Component*>(component)->*member);
            }};
        for(const auto& [value, option] : options)
            descriptor.enum_options.push_back(option);
        descriptor.read_enum = [options](const void* value) -> std::optional<std::string> {
            for(const auto& [enumerator, option] : options)
                if(*static_cast<const Enum*>(value) == enumerator)
                    return option.id;
            return std::nullopt;
        };
        descriptor.write_enum = [options](void* value, std::string_view id) {
            for(const auto& [enumerator, option] : options)
                if(option.id == id) {
                    *static_cast<Enum*>(value) = enumerator;
                    return true;
                }
            return false;
        };
        return descriptor;
    }

}
