#include "asset/serialization/material_serializer.h"
#include "asset/serialization/json_serialization.h"

#include <cmath>
#include <unordered_set>
#include <utility>

namespace Comet {
    namespace {
        AssetResult<void> validate_material_data(
            const MaterialData& data, const Json::Context& context) {
            if(data.template_name.empty()) {
                return AssetResult<void>::failure(
                    context.error("template", "expected a non-empty string"));
            }
            std::unordered_set<std::string_view> names;
            const auto validate_name = [&](const std::string& name) {
                if(name.empty() || !names.insert(name).second) {
                    return AssetResult<void>::failure(context.error("properties",
                        "property names must be non-empty and unique across types"));
                }
                return AssetResult<void>::success();
            };
            for(const auto& [property_name, texture_handle] : data.texture_properties) {
                if(auto valid = validate_name(property_name); !valid)
                    return valid;
                if(!texture_handle) {
                    return AssetResult<void>::failure(
                        context.error("properties." + property_name + ".asset",
                            "expected a non-zero unsigned integer"));
                }
            }
            for(const auto& [name, value] : data.scalar_properties) {
                if(auto valid = validate_name(name); !valid)
                    return valid;
                if(!std::isfinite(value))
                    return AssetResult<void>::failure(context.error(
                        "properties." + name + ".value", "expected a finite number"));
            }
            for(const auto& [name, value] : data.vector_properties) {
                if(auto valid = validate_name(name); !valid)
                    return valid;
                if(!Math::is_finite(value))
                    return AssetResult<void>::failure(context.error(
                        "properties." + name + ".value", "expected four finite numbers"));
            }
            return AssetResult<void>::success();
        }

        AssetResult<void> encode_material(const MaterialData& data,
            const Json::Context& context, Json::Writer& writer) {
            if(auto valid = validate_material_data(data, context); !valid)
                return valid;

            writer.begin_object();
            writer.field("version", std::uint64_t(MaterialSerializer::FORMAT_VERSION));
            writer.field("template", data.template_name);

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

            return AssetResult<void>::success();
        }

        AssetResult<MaterialData> decode_material(
            const Json::Node& root, const Json::Context& context) {
            context.validate_keys(root, {"version", "template", "properties"});

            const std::uint32_t version = context.read_scalar<std::uint32_t>(
                context.required_child(root, "version"), "version",
                "an unsigned integer");
            if(version != MaterialSerializer::FORMAT_VERSION) {
                return AssetResult<MaterialData>::failure(context.error(
                    "version", "unsupported version " + std::to_string(version)));
            }

            MaterialData data;
            data.template_name =
                context.read_scalar<std::string>(context.required_child(root, "template"),
                    "template", "a non-empty string");
            if(data.template_name.empty()) {
                return AssetResult<MaterialData>::failure(
                    context.error("template", "expected a non-empty string"));
            }

            const Json::Node properties = context.required_child(root, "properties");
            for(const auto entry : context.object(properties, "properties")) {
                const std::string property_name(entry.key);
                if(property_name.empty()) {
                    return AssetResult<MaterialData>::failure(
                        context.error("properties", "property names cannot be empty"));
                }
                const std::string property_location = "properties." + property_name;
                const Json::Node property = entry.value;
                const std::string type = context.read_scalar<std::string>(
                    context.required_child(property, "type", property_location),
                    property_location + ".type", "a string");
                if(type == "scalar" || type == "vector") {
                    context.validate_keys(property, {"type", "value"}, property_location);
                    const auto value =
                        context.required_child(property, "value", property_location);
                    const auto location = property_location + ".value";
                    if(type == "scalar") {
                        data.scalar_properties.emplace(
                            property_name, context.read_scalar<float>(
                                               value, location, "a finite number"));
                    } else {
                        const auto elements = context.array(value, location);
                        if(elements.size() != 4)
                            return AssetResult<MaterialData>::failure(
                                context.error(location, "expected four finite numbers"));
                        Math::Vec4 vector(0.0f);
                        int index = 0;
                        for(const auto element : elements) {
                            vector[index] = context.read_scalar<float>(element,
                                location + "[" + std::to_string(index) + "]",
                                "a finite number");
                            ++index;
                        }
                        data.vector_properties.emplace(property_name, vector);
                    }
                    continue;
                }
                if(type != "texture") {
                    return AssetResult<MaterialData>::failure(
                        context.error(property_location + ".type",
                            "unsupported property type '" + type + "'"));
                }

                context.validate_keys(property, {"type", "asset"}, property_location);
                const std::uint64_t asset = context.read_scalar<std::uint64_t>(
                    context.required_child(property, "asset", property_location),
                    property_location + ".asset", "a non-zero unsigned integer");
                const AssetHandle texture_handle(asset);
                if(!texture_handle) {
                    return AssetResult<MaterialData>::failure(
                        context.error(property_location + ".asset",
                            "expected a non-zero unsigned integer"));
                }
                data.texture_properties.emplace(property_name, texture_handle);
            }

            return AssetResult<MaterialData>::success(std::move(data));
        }
    }

    AssetResult<std::string> MaterialSerializer::serialize(
        const MaterialData& data) const {
        return AssetSerialization::serialize_json("material", data, encode_material);
    }

    AssetResult<MaterialData> MaterialSerializer::deserialize(
        const std::string_view contents, const std::string_view source) const {
        return AssetSerialization::deserialize_json<MaterialData>(
            "material", contents, source, decode_material);
    }

    AssetResult<void> MaterialSerializer::save(
        const MaterialData& data, const std::filesystem::path& path) const {
        return AssetSerialization::save(*this, data, path);
    }

    AssetResult<MaterialData> MaterialSerializer::load(
        const std::filesystem::path& path) const {
        return AssetSerialization::load(*this, path);
    }
}
