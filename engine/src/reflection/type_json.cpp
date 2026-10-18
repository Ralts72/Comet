#include "reflection/type_json.h"
#include "common/serialization/parameters_json.h"
#include <algorithm>
#include <unordered_set>

namespace Comet::ReflectionJson {
    void write_entity_reference(Json::Writer& writer, const EntityUuid value) {
        writer.begin_object();
        writer.field("entity", value.to_string());
        writer.end_object();
    }

    Result<Uuid> read_entity_reference(
        Json::Node node, const Json::Context& context, const std::string_view location) {
        if(auto valid = context.validate_keys(node, {"entity"}, location); !valid)
            return Result<Uuid>::failure(valid.error());
        auto text = context.read_field<std::string>(node, "entity", "a UUID string", location);
        if(!text)
            return Result<Uuid>::failure(text.error());
        const auto uuid = EntityUuid::parse(text.value());
        if(!uuid)
            return Result<Uuid>::failure(
                context.error(location, "expected a canonical entity UUID"));
        return Result<Uuid>::success(*uuid);
    }

    Result<PropertyValue> read_unchecked(const PropertyDescriptor& property, const Json::Node& node,
        const Json::Context& context, const std::string_view location) {
        switch(property.type) {
            case PropertyType::Object: {
                auto object = context.object(node, location);
                if(!object)
                    return Result<PropertyValue>::failure(object.error());
                PropertyCollection::Data data;
                std::unordered_set<std::string_view> seen;
                for(const auto field : object.value()) {
                    const auto member =
                        std::ranges::find(property.members, field.key, &PropertyDescriptor::id);
                    if(member == property.members.end() || !member->serializable
                        || member->transient)
                        return Result<PropertyValue>::failure(
                            context.error(location, "unknown field: " + std::string(field.key)));
                    if(!seen.insert(field.key).second)
                        return Result<PropertyValue>::failure(
                            context.error(location, "duplicate field: " + std::string(field.key)));
                    auto child = read_value(*member, field.value, context,
                        std::string(location) + "." + std::string(field.key));
                    if(!child)
                        return child;
                    data.fields.emplace_back(field.key, std::move(child).value());
                }
                return Result<PropertyValue>::success(PropertyCollection(std::move(data)));
            }
            case PropertyType::Sequence:
            case PropertyType::Optional: {
                PropertyCollection::Data data;
                data.kind = property.type == PropertyType::Sequence
                                ? PropertyCollection::Kind::Sequence
                                : PropertyCollection::Kind::Optional;
                if(property.type == PropertyType::Optional) {
                    if(!node.is_null()) {
                        auto value = read_value(*property.element, node, context, location);
                        if(!value)
                            return value;
                        data.elements.push_back(std::move(value).value());
                    }
                } else {
                    auto array = context.array(node, location);
                    if(!array)
                        return Result<PropertyValue>::failure(array.error());
                    if(array.value().size() > property.max_elements)
                        return Result<PropertyValue>::failure(
                            context.error(location, "too many elements"));
                    for(const auto element : array.value()) {
                        auto value = read_value(*property.element, element, context,
                            std::string(location) + "[" + std::to_string(data.elements.size())
                                + "]");
                        if(!value)
                            return value;
                        data.elements.push_back(std::move(value).value());
                    }
                }
                return Result<PropertyValue>::success(PropertyCollection(std::move(data)));
            }
            case PropertyType::Int32: {
                auto value =
                    context.read_scalar<int32_t>(node, location, "a signed 32-bit integer");
                if(!value)
                    return Result<PropertyValue>::failure(value.error());
                return Result<PropertyValue>::success(value.value());
            }
            case PropertyType::Parameters: {
                auto parameters =
                    Json::read_parameters(node, context, location, read_entity_reference);
                if(!parameters)
                    return Result<PropertyValue>::failure(parameters.error());
                return Result<PropertyValue>::success(std::move(parameters).value());
            }
            case PropertyType::Bool: {
                auto value = context.read_scalar<bool>(node, location, "a boolean");
                if(!value)
                    return Result<PropertyValue>::failure(value.error());
                return Result<PropertyValue>::success(std::move(value).value());
            }
            case PropertyType::Enum:
            case PropertyType::String: {
                auto value = context.read_scalar<std::string>(node, location, "a string");
                if(!value)
                    return Result<PropertyValue>::failure(value.error());
                if(property.type == PropertyType::Enum
                    && std::ranges::none_of(property.enum_options,
                        [&](const auto& option) { return option.id == value.value(); }))
                    return Result<PropertyValue>::failure(
                        context.error(location, "unknown enum name: " + value.value()));
                return Result<PropertyValue>::success(std::move(value).value());
            }
            case PropertyType::UInt32: {
                auto value =
                    context.read_scalar<uint32_t>(node, location, "an unsigned 32-bit integer");
                if(!value)
                    return Result<PropertyValue>::failure(value.error());
                return Result<PropertyValue>::success(value.value());
            }
            case PropertyType::Float: {
                auto value = context.read_scalar<float>(node, location, "a finite number");
                if(!value)
                    return Result<PropertyValue>::failure(value.error());
                if(!property.accepts_value(value.value()))
                    return Result<PropertyValue>::failure(
                        context.error(location, "value outside allowed bounds"));
                return Result<PropertyValue>::success(std::move(value).value());
            }
            case PropertyType::Vec3: {
                auto value = Json::read_vector<Math::Vec3>(node, context, location);
                if(!value)
                    return Result<PropertyValue>::failure(value.error());
                if(!property.accepts_value(value.value()))
                    return Result<PropertyValue>::failure(
                        context.error(location, "value outside allowed bounds"));
                return Result<PropertyValue>::success(value.value());
            }
            case PropertyType::Vec4: {
                auto value = Json::read_vector<Math::Vec4>(node, context, location);
                if(!value)
                    return Result<PropertyValue>::failure(value.error());
                if(!property.accepts_value(value.value()))
                    return Result<PropertyValue>::failure(
                        context.error(location, "value outside allowed bounds"));
                return Result<PropertyValue>::success(value.value());
            }
            case PropertyType::AssetHandle: {
                auto value = context.read_scalar<AssetHandle::ValueType>(
                    node, location, "a non-negative integer");
                if(!value)
                    return Result<PropertyValue>::failure(value.error());
                return Result<PropertyValue>::success(AssetHandle(value.value()));
            }
            case PropertyType::EntityReference: {
                auto reference = read_entity_reference(node, context, location);
                if(!reference)
                    return Result<PropertyValue>::failure(reference.error());
                return Result<PropertyValue>::success(reference.value());
            }
        }
        return Result<PropertyValue>::failure(context.error(location, "unsupported property type"));
    }

    Result<PropertyValue> read_value(const PropertyDescriptor& property, Json::Node node,
        const Json::Context& context, std::string_view location) {
        auto result = read_unchecked(property, node, context, location);
        if(result && !property.accepts_value(result.value()))
            return Result<PropertyValue>::failure(
                context.error(location, "value violates property constraints or required fields"));
        return result;
    }
    namespace {
        bool valid_text(const PropertyValue& value) {
            if(const auto* text = std::get_if<std::string>(&value))
                return Json::valid_utf8(*text);
            if(const auto* parameters = std::get_if<ParameterMap>(&value)) {
                for(const auto& [name, parameter] : *parameters) {
                    const auto* text = std::get_if<std::string>(&parameter);
                    if(!Json::valid_utf8(name) || (text && !Json::valid_utf8(*text)))
                        return false;
                }
            }
            if(const auto* collection = std::get_if<PropertyCollection>(&value)) {
                for(const auto& [name, child] : collection->data().fields)
                    if(!Json::valid_utf8(name) || !valid_text(child))
                        return false;
                for(const auto& child : collection->data().elements)
                    if(!valid_text(child))
                        return false;
            }
            return true;
        }
    }
    Result<PropertyValue> copy_value(const PropertyDescriptor& property, const void* object,
        const Json::Context& context, std::string_view location) {
        auto value = property.copy_value(object);
        if(!value)
            return Result<PropertyValue>::failure(
                context.error(location, "property accessor returned null or unsupported type"));
        if(!property.accepts_value(*value)) {
            const char* message = "value violates property constraints";
            if(property.type == PropertyType::Parameters)
                message = "Invalid parameters";
            else if(property.type == PropertyType::Float || property.type == PropertyType::Vec3
                    || property.type == PropertyType::Vec4)
                message = "expected a finite value within property constraints";
            return Result<PropertyValue>::failure(context.error(location, message));
        }
        if(!valid_text(*value))
            return Result<PropertyValue>::failure(context.error(location, "invalid UTF-8"));
        return Result<PropertyValue>::success(std::move(*value));
    }
    Result<void> write_value(Json::Writer& writer, const PropertyDescriptor& property,
        const PropertyValue& source, const Json::Context& context, std::string_view location) {
        return std::visit(
            [&](const auto& value) -> Result<void> {
                using T = std::remove_cvref_t<decltype(value)>;
                if constexpr(std::is_same_v<T, PropertyCollection>) {
                    const auto& data = value.data();
                    if(property.type == PropertyType::Object) {
                        writer.begin_object();
                        for(const auto& [name, child] : data.fields) {
                            const auto descriptor =
                                std::ranges::find(property.members, name, &PropertyDescriptor::id);
                            if(descriptor == property.members.end())
                                return Result<void>::failure(
                                    context.error(location, "unknown reflected field"));
                            if(!descriptor->serializable || descriptor->transient)
                                continue;
                            if(descriptor->omit_default && descriptor->default_value
                                && descriptor->default_value() == child)
                                continue;
                            writer.key(name);
                            if(auto result = write_value(writer, *descriptor, child, context,
                                   std::string(location) + "." + name);
                                !result)
                                return result;
                        }
                        writer.end_object();
                    } else if(property.type == PropertyType::Optional) {
                        if(data.elements.empty())
                            writer.null_value();
                        else
                            return write_value(writer, *property.element, data.elements.front(),
                                context, location);
                    } else {
                        writer.begin_array();
                        for(const auto& child : data.elements)
                            if(auto result = write_value(
                                   writer, *property.element, child, context, location);
                                !result)
                                return result;
                        writer.end_array();
                    }
                } else if constexpr(std::is_same_v<T, Math::Vec3> || std::is_same_v<T, Math::Vec4>)
                    Json::write_vector(writer, value);
                else if constexpr(std::is_same_v<T, AssetHandle>)
                    writer.value(value.value());
                else if constexpr(std::is_same_v<T, uint32_t>)
                    writer.value(uint64_t{value});
                else if constexpr(std::is_same_v<T, int32_t>)
                    writer.value(int64_t{value});
                else if constexpr(std::is_same_v<T, EntityUuid>)
                    write_entity_reference(writer, value);
                else if constexpr(std::is_same_v<T, ParameterMap>)
                    return Json::write_parameters(
                        writer, value, context, location, write_entity_reference);
                else
                    writer.value(value);
                return Result<void>::success();
            },
            source);
    }
}
