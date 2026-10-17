#include "common/serialization/parameters_json.h"

namespace Comet::Json {
    namespace {
        template<typename T> Result<ParameterValue> parameter_value(Result<T> value) {
            if(!value)
                return Result<ParameterValue>::failure(value.error());
            return Result<ParameterValue>::success(std::move(value).value());
        }

        Result<ParameterValue> read_value(Node node, const Context& context,
            std::string_view location, ParameterReferenceReader read_reference) {
            if(node.is_bool())
                return parameter_value(context.read_scalar<bool>(node, location, "a boolean"));
            if(node.is_number())
                return parameter_value(
                    context.read_scalar<float>(node, location, "a finite number"));
            if(node.is_string())
                return parameter_value(
                    context.read_scalar<std::string>(node, location, "a string"));
            if(node.is_array()) {
                auto elements = context.array(node, location);
                if(!elements)
                    return Result<ParameterValue>::failure(elements.error());
                if(elements.value().size() == 3)
                    return parameter_value(read_vector<Math::Vec3>(node, context, location));
                if(elements.value().size() == 4)
                    return parameter_value(read_vector<Math::Vec4>(node, context, location));
                return Result<ParameterValue>::failure(
                    context.error(location, "expected exactly 3 or 4 numbers"));
            }
            if(node.is_object() && read_reference)
                return parameter_value(read_reference(node, context, location));
            return Result<ParameterValue>::failure(
                context.error(location, "unsupported parameter type"));
        }
    }

    Result<ParameterMap> read_parameters(Node node, const Context& context,
        std::string_view location, ParameterReferenceReader read_reference) {
        auto object = context.object(node, location);
        if(!object)
            return Result<ParameterMap>::failure(object.error());
        ParameterMap parameters;
        for(const auto field : object.value()) {
            const auto field_location = std::string(location) + "." + std::string(field.key);
            if(!valid_parameter_name(field.key))
                return Result<ParameterMap>::failure(
                    context.error(field_location, "invalid parameter name"));
            if(parameters.size() >= MAX_PARAMETER_COUNT)
                return Result<ParameterMap>::failure(
                    context.error(location, "too many parameters"));
            auto value = read_value(field.value, context, field_location, read_reference);
            if(!value)
                return Result<ParameterMap>::failure(value.error());
            if(!valid_parameter_value(value.value()))
                return Result<ParameterMap>::failure(
                    context.error(field_location, "invalid parameter value"));
            parameters.emplace(field.key, std::move(value).value());
        }
        return Result<ParameterMap>::success(std::move(parameters));
    }

    Result<void> write_parameters(Writer& writer, const ParameterMap& parameters,
        const Context& context, std::string_view location,
        ParameterReferenceWriter write_reference) {
        if(!valid_parameters(parameters))
            return Result<void>::failure(context.error(location, "invalid parameters"));
        for(const auto& [name, value] : parameters)
            if(std::holds_alternative<Uuid>(value) && !write_reference)
                return Result<void>::failure(context.error(
                    std::string(location) + "." + name, "unsupported parameter reference"));
        writer.begin_object();
        for(const auto& [name, value] : parameters) {
            writer.key(name);
            std::visit(
                [&](const auto& item) {
                    using T = std::remove_cvref_t<decltype(item)>;
                    if constexpr(std::is_same_v<T, Math::Vec3> || std::is_same_v<T, Math::Vec4>)
                        write_vector(writer, item);
                    else if constexpr(std::is_same_v<T, Uuid>)
                        write_reference(writer, item);
                    else
                        writer.value(item);
                },
                value);
        }
        writer.end_object();
        return Result<void>::success();
    }
}
