#include "asset/serialization/material_serializer.h"
#include "asset/serialization/yaml_serialization.h"

#include <utility>

namespace Comet {
    namespace {
        AssetResult<void> validate_material_data(
            const MaterialData& data, const AssetSerialization::YamlContext& context) {
            if(data.template_name.empty()) {
                return AssetResult<void>::failure(
                    context.error("template", "expected a non-empty string"));
            }
            for(const auto& [property_name, texture_handle] : data.texture_properties) {
                if(property_name.empty()) {
                    return AssetResult<void>::failure(
                        context.error("properties", "property names cannot be empty"));
                }
                if(!texture_handle) {
                    return AssetResult<void>::failure(
                        context.error("properties." + property_name + ".asset",
                            "expected a non-zero unsigned integer"));
                }
            }
            return AssetResult<void>::success();
        }

        AssetResult<YAML::Node> encode_material(
            const MaterialData& data, const AssetSerialization::YamlContext& context) {
            if(auto valid = validate_material_data(data, context); !valid)
                return AssetResult<YAML::Node>::failure(valid.error());

            YAML::Node root(YAML::NodeType::Map);
            root["version"] = MaterialSerializer::FORMAT_VERSION;
            root["template"] = data.template_name;

            YAML::Node properties(YAML::NodeType::Map);
            for(const auto& [property_name, texture_handle] : data.texture_properties) {
                YAML::Node property(YAML::NodeType::Map);
                property["type"] = "texture";
                property["asset"] = texture_handle.value();
                properties[property_name] = property;
            }
            root["properties"] = properties;

            return AssetResult<YAML::Node>::success(std::move(root));
        }

        AssetResult<MaterialData> decode_material(
            const YAML::Node& root, const AssetSerialization::YamlContext& context) {
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

            const YAML::Node properties = context.required_child(root, "properties");
            context.require_map(properties, "properties");
            for(const auto& entry : properties) {
                if(!entry.first.IsScalar()) {
                    return AssetResult<MaterialData>::failure(
                        context.error("properties", "expected string keys"));
                }

                const std::string property_name = entry.first.as<std::string>();
                if(property_name.empty()) {
                    return AssetResult<MaterialData>::failure(
                        context.error("properties", "property names cannot be empty"));
                }
                if(data.texture_properties.contains(property_name)) {
                    return AssetResult<MaterialData>::failure(context.error(
                        "properties", "duplicate property '" + property_name + "'"));
                }

                const std::string property_location = "properties." + property_name;
                const YAML::Node property = entry.second;
                context.validate_keys(property, {"type", "asset"}, property_location);

                const std::string type = context.read_scalar<std::string>(
                    context.required_child(property, "type", property_location),
                    property_location + ".type", "a string");
                if(type != "texture") {
                    return AssetResult<MaterialData>::failure(
                        context.error(property_location + ".type",
                            "unsupported property type '" + type + "'"));
                }

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
        return AssetSerialization::serialize_yaml("material", data, encode_material);
    }

    AssetResult<MaterialData> MaterialSerializer::deserialize(
        const std::string_view contents, const std::string_view source) const {
        return AssetSerialization::deserialize_yaml<MaterialData>(
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
