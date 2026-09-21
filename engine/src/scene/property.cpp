#include "scene/property.h"
#include <cmath>

namespace Comet {
    bool valid_parameters(const ParameterMap& parameters) {
        if(parameters.size() > 128)
            return false;
        for(const auto& [name, value] : parameters) {
            if(name.empty() || name.size() > 128 || name.find('\0') != std::string::npos)
                return false;
            const bool valid = std::visit(
                [](const auto& item) {
                    using T = std::remove_cvref_t<decltype(item)>;
                    if constexpr(std::is_same_v<T, float>)
                        return std::isfinite(item);
                    else if constexpr(std::is_same_v<T, Math::Vec3>)
                        return Math::is_finite(item);
                    else if constexpr(std::is_same_v<T, std::string>)
                        return item.size() <= 4096;
                    else
                        return true;
                },
                value);
            if(!valid)
                return false;
        }
        return true;
    }
    bool property_values_equal(const PropertyValue& left, const PropertyValue& right) {
        if(left.index() != right.index()) {
            return false;
        }
        return std::visit(
            [&right](const auto& value) {
                using Value = std::remove_cvref_t<decltype(value)>;
                return value == std::get<Value>(right);
            },
            left);
    }

    std::optional<PropertyValue> PropertyDescriptor::copy_value(const void* component) const {
        const void* value = get_value(component);
        if(!value) {
            return std::nullopt;
        }
        switch(type) {
            case PropertyType::Parameters:
                return *static_cast<const ParameterMap*>(value);
            case PropertyType::Bool:
                return *static_cast<const bool*>(value);
            case PropertyType::Float:
                return *static_cast<const float*>(value);
            case PropertyType::Vec3:
                return *static_cast<const Math::Vec3*>(value);
            case PropertyType::AssetHandle:
                return *static_cast<const AssetHandle*>(value);
            case PropertyType::String:
                return *static_cast<const std::string*>(value);
            case PropertyType::Enum:
                if(read_enum) {
                    if(auto name = read_enum(value))
                        return PropertyValue(*name);
                }
                return std::nullopt;
        }
        return std::nullopt;
    }

    bool PropertyDescriptor::assign_value(void* component, const PropertyValue& value) const {
        void* destination = get_value(component);
        if(!destination || !editable || read_only) {
            return false;
        }
        if(type == PropertyType::Enum) {
            const auto* name = std::get_if<std::string>(&value);
            if(!name || !write_enum || !write_enum(destination, *name))
                return false;
            notify_changed(destination);
            return true;
        }
        const bool assigned = std::visit(
            [this, destination](const auto& source) {
                using Value = std::remove_cvref_t<decltype(source)>;
                if constexpr(std::is_same_v<Value, bool>) {
                    if(type != PropertyType::Bool)
                        return false;
                } else if constexpr(std::is_same_v<Value, float>) {
                    if(type != PropertyType::Float || !std::isfinite(source))
                        return false;
                } else if constexpr(std::is_same_v<Value, Math::Vec3>) {
                    if(type != PropertyType::Vec3 || !Math::is_finite(source))
                        return false;
                } else if constexpr(std::is_same_v<Value, std::string>) {
                    if(type != PropertyType::String)
                        return false;
                } else if constexpr(std::is_same_v<Value, ParameterMap>) {
                    if(type != PropertyType::Parameters || !valid_parameters(source))
                        return false;
                } else {
                    if(type != PropertyType::AssetHandle)
                        return false;
                }
                *static_cast<Value*>(destination) = source;
                return true;
            },
            value);
        if(assigned) {
            notify_changed(destination);
        }
        return assigned;
    }

}
