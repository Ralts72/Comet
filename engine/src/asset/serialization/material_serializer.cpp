#include "asset/serialization/material_serializer.h"
#include "asset/serialization/json_serialization.h"

#include <cmath>
#include <unordered_set>
#include <utility>

namespace Comet {
    namespace {
        Result<void> validate_material_data(
            const MaterialData& data, const Json::Context& context) {
            if(data.template_name.empty()) {
                return Result<void>::failure(
                    context.error("template", "expected a non-empty string"));
            }
            std::unordered_set<std::string_view> names;
            const auto validate_name = [&](const std::string& name) {
                if(name.empty() || !names.insert(name).second) {
                    return Result<void>::failure(context.error(
                        "properties", "property names must be non-empty and unique across types"));
                }
                return Result<void>::success();
            };
            for(const auto& [property_name, texture_handle] : data.texture_properties) {
                if(auto valid = validate_name(property_name); !valid)
                    return valid;
                if(!texture_handle) {
                    return Result<void>::failure(
                        context.error("properties." + property_name + ".asset",
                            "expected a non-zero unsigned integer"));
                }
            }
            for(const auto& [name, value] : data.scalar_properties) {
                if(auto valid = validate_name(name); !valid)
                    return valid;
                if(!std::isfinite(value))
                    return Result<void>::failure(
                        context.error("properties." + name + ".value", "expected a finite number"));
            }
            for(const auto& [name, value] : data.vector_properties) {
                if(auto valid = validate_name(name); !valid)
                    return valid;
                if(!Math::is_finite(value))
                    return Result<void>::failure(context.error(
                        "properties." + name + ".value", "expected four finite numbers"));
            }
            return Result<void>::success();
        }

        Result<void> encode_material(
            const MaterialData& data, const Json::Context& context, Json::Writer& writer) {
            if(auto valid = validate_material_data(data, context); !valid)
                return valid;

            writer.begin_object();
            writer.field("version", std::uint64_t(MaterialSerializer::FORMAT_VERSION));
            writer.field("template", data.template_name);
            if(data.shader_program)
                writer.field("shader_program", data.shader_program.value());

            writer.key("properties");
            writer.begin_object();
            for(const auto& [property_name, texture_handle] : data.texture_properties) {
                writer.key(property_name);
                writer.begin_object();
                writer.field("type", "texture");
                writer.field("asset", texture_handle.value());
                writer.end_object();
            }
            for(const auto& [name, value] : data.scalar_properties) {
                writer.key(name);
                writer.begin_object();
                writer.field("type", "scalar");
                writer.field("value", value);
                writer.end_object();
            }
            for(const auto& [name, value] : data.vector_properties) {
                writer.key(name);
                writer.begin_object();
                writer.field("type", "vector");
                writer.key("value");
                writer.begin_array();
                for(int component = 0; component < 4; ++component)
                    writer.value(value[component]);
                writer.end_array();
                writer.end_object();
            }
            writer.end_object();
            writer.end_object();

            return Result<void>::success();
        }

        Result<MaterialData> decode_material(const Json::Node& root, const Json::Context& context) {
            if(auto valid = context.validate_keys(
                   root, {"version", "template", "shader_program", "properties"});
                !valid)
                return Result<MaterialData>::failure(valid.error());

            const auto version =
                context.read_field<std::uint32_t>(root, "version", "an unsigned integer");
            if(!version)
                return Result<MaterialData>::failure(version.error());
            if(version.value() != MaterialSerializer::FORMAT_VERSION) {
                return Result<MaterialData>::failure(context.error(
                    "version", "unsupported version " + std::to_string(version.value())));
            }

            MaterialData data;
            auto template_name =
                context.read_field<std::string>(root, "template", "a non-empty string");
            if(!template_name)
                return Result<MaterialData>::failure(template_name.error());
            data.template_name = std::move(template_name).value();
            if(data.template_name.empty()) {
                return Result<MaterialData>::failure(
                    context.error("template", "expected a non-empty string"));
            }

            Json::Node program;
            if(!root["shader_program"].get(program)) {
                const auto handle = context.read_scalar<AssetHandle::ValueType>(
                    program, "shader_program", "a non-zero unsigned integer");
                if(!handle)
                    return Result<MaterialData>::failure(handle.error());
                data.shader_program = AssetHandle(handle.value());
                if(!data.shader_program)
                    return Result<MaterialData>::failure(
                        context.error("shader_program", "expected a non-zero value"));
            }

            const auto properties = context.required_child(root, "properties");
            if(!properties)
                return Result<MaterialData>::failure(properties.error());
            const auto fields = context.object(properties.value(), "properties");
            if(!fields)
                return Result<MaterialData>::failure(fields.error());
            for(const auto entry : fields.value()) {
                const std::string property_name(entry.key);
                if(property_name.empty()) {
                    return Result<MaterialData>::failure(
                        context.error("properties", "property names cannot be empty"));
                }
                const std::string property_location = "properties." + property_name;
                const Json::Node property = entry.value;
                const auto type_result = context.read_field<std::string>(
                    property, "type", "a string", property_location);
                if(!type_result)
                    return Result<MaterialData>::failure(type_result.error());
                const auto& type = type_result.value();
                if(type == "scalar" || type == "vector") {
                    if(auto valid =
                            context.validate_keys(property, {"type", "value"}, property_location);
                        !valid)
                        return Result<MaterialData>::failure(valid.error());
                    const auto value = context.required_child(property, "value", property_location);
                    if(!value)
                        return Result<MaterialData>::failure(value.error());
                    const auto location = property_location + ".value";
                    if(type == "scalar") {
                        const auto scalar =
                            context.read_scalar<float>(value.value(), location, "a finite number");
                        if(!scalar)
                            return Result<MaterialData>::failure(scalar.error());
                        data.scalar_properties.emplace(property_name, scalar.value());
                    } else {
                        const auto elements = context.array(value.value(), location);
                        if(!elements)
                            return Result<MaterialData>::failure(elements.error());
                        if(elements.value().size() != 4)
                            return Result<MaterialData>::failure(
                                context.error(location, "expected four finite numbers"));
                        Math::Vec4 vector(0.0f);
                        int index = 0;
                        for(const auto element : elements.value()) {
                            const auto scalar = context.read_scalar<float>(element,
                                location + "[" + std::to_string(index) + "]", "a finite number");
                            if(!scalar)
                                return Result<MaterialData>::failure(scalar.error());
                            vector[index] = scalar.value();
                            ++index;
                        }
                        data.vector_properties.emplace(property_name, vector);
                    }
                    continue;
                }
                if(type != "texture") {
                    return Result<MaterialData>::failure(context.error(
                        property_location + ".type", "unsupported property type '" + type + "'"));
                }

                if(auto valid =
                        context.validate_keys(property, {"type", "asset"}, property_location);
                    !valid)
                    return Result<MaterialData>::failure(valid.error());
                const auto asset = context.read_field<std::uint64_t>(
                    property, "asset", "a non-zero unsigned integer", property_location);
                if(!asset)
                    return Result<MaterialData>::failure(asset.error());
                const AssetHandle texture_handle(asset.value());
                if(!texture_handle) {
                    return Result<MaterialData>::failure(context.error(
                        property_location + ".asset", "expected a non-zero unsigned integer"));
                }
                data.texture_properties.emplace(property_name, texture_handle);
            }

            return Result<MaterialData>::success(std::move(data));
        }
    }

    Result<std::string> MaterialSerializer::serialize(const MaterialData& data) const {
        return AssetSerialization::serialize_json("material", data, encode_material);
    }

    Result<MaterialData> MaterialSerializer::deserialize(
        const std::string_view contents, const std::string_view source) const {
        return AssetSerialization::deserialize_json<MaterialData>(
            "material", contents, source, decode_material);
    }

    Result<void> MaterialSerializer::save(
        const MaterialData& data, const std::filesystem::path& path) const {
        return AssetSerialization::save(*this, data, path);
    }

    Result<MaterialData> MaterialSerializer::load(const std::filesystem::path& path) const {
        return AssetSerialization::load(*this, path);
    }
}
