#include "asset/serialization/material_serializer.h"
#include "common/file_io.h"
#include "common/yaml_utils.h"

#include <yaml-cpp/yaml.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <cmath>
#include <algorithm>

namespace Comet {
    namespace {
        std::runtime_error material_error(const std::string_view source,
            const std::string_view location, const std::string& detail) {
            return std::runtime_error("Invalid material '" + std::string(source)
                                      + "' at '" + std::string(location)
                                      + "': " + detail);
        }

        void require_map(const YAML::Node& node, const std::string_view source,
            const std::string_view location) {
            Yaml::require_map(node, source, location, material_error);
        }

        void validate_keys(const YAML::Node& node,
            const std::unordered_set<std::string>& supported,
            const std::string_view source, const std::string_view location) {
            Yaml::validate_keys(node, supported, source, location, material_error);
        }

        YAML::Node required_child(const YAML::Node& node, const char* key,
            const std::string_view source, const std::string_view location) {
            return Yaml::required_child(node, key, source, location, material_error);
        }

        template<typename T>
        T read_scalar(const YAML::Node& node, const std::string_view source,
            const std::string_view location, const std::string_view expected) {
            return Yaml::read_scalar<T>(node, source, location, expected, material_error);
        }

        void validate_material_data(
            const MaterialData& data, const std::string_view source) {
            if(data.template_name.empty()) {
                throw material_error(source, "template", "expected a non-empty string");
            }
            std::unordered_set<std::string> names;
            const auto validate_name = [&](const std::string& name) {
                if(name.empty() || !names.insert(name).second) {
                    throw material_error(
                        source, "properties", "empty or duplicate property name");
                }
            };
            for(const auto& [property_name, texture_handle] : data.texture_properties) {
                validate_name(property_name);
                if(property_name.empty()) {
                    throw material_error(
                        source, "properties", "property names cannot be empty");
                }
                if(!texture_handle) {
                    throw material_error(source, "properties." + property_name + ".asset",
                        "expected a non-zero unsigned integer");
                }
            }
            for(const auto& [name, value] : data.scalar_properties) {
                validate_name(name);
                if(!std::isfinite(value)) {
                    throw material_error(
                        source, "properties." + name, "expected a finite scalar");
                }
            }
            for(const auto& [name, value] : data.vector_properties) {
                validate_name(name);
                if(!std::ranges::all_of(
                       value, [](float item) { return std::isfinite(item); })) {
                    throw material_error(
                        source, "properties." + name, "expected four finite components");
                }
            }
        }
    }

    std::string MaterialSerializer::serialize(const MaterialData& data) const {
        validate_material_data(data, "<memory>");

        YAML::Node root(YAML::NodeType::Map);
        root["version"] = FORMAT_VERSION;
        root["template"] = data.template_name;

        YAML::Node properties(YAML::NodeType::Map);
        for(const auto& [property_name, texture_handle] : data.texture_properties) {
            YAML::Node property(YAML::NodeType::Map);
            property["type"] = "texture";
            property["asset"] = texture_handle.value();
            properties[property_name] = property;
        }
        for(const auto& [name, value] : data.scalar_properties) {
            properties[name]["type"] = "scalar";
            properties[name]["value"] = value;
        }
        for(const auto& [name, value] : data.vector_properties) {
            properties[name]["type"] = "vector";
            YAML::Node components(YAML::NodeType::Sequence);
            for(const float component : value)
                components.push_back(component);
            properties[name]["value"] = components;
        }
        root["properties"] = properties;

        YAML::Emitter emitter;
        emitter << root;
        if(!emitter.good()) {
            throw std::runtime_error(
                "Failed to serialize material: " + emitter.GetLastError());
        }
        return std::string(emitter.c_str()) + '\n';
    }

    MaterialData MaterialSerializer::deserialize(
        const std::string_view contents, const std::string_view source) const {
        YAML::Node root;
        try {
            root = YAML::Load(std::string(contents));
        } catch(const YAML::Exception& exception) {
            throw material_error(source, "<yaml>", exception.what());
        }

        require_map(root, source, "<root>");
        validate_keys(root, {"version", "template", "properties"}, source, "<root>");

        const std::uint32_t version =
            read_scalar<std::uint32_t>(required_child(root, "version", source, "<root>"),
                source, "version", "an unsigned integer");
        if(version != FORMAT_VERSION) {
            throw material_error(
                source, "version", "unsupported version " + std::to_string(version));
        }

        MaterialData data;
        data.template_name =
            read_scalar<std::string>(required_child(root, "template", source, "<root>"),
                source, "template", "a non-empty string");
        if(data.template_name.empty()) {
            throw material_error(source, "template", "expected a non-empty string");
        }

        const YAML::Node properties =
            required_child(root, "properties", source, "<root>");
        require_map(properties, source, "properties");
        std::unordered_set<std::string> names;
        for(const auto& entry : properties) {
            if(!entry.first.IsScalar()) {
                throw material_error(source, "properties", "expected string keys");
            }

            const std::string property_name = entry.first.as<std::string>();
            if(property_name.empty()) {
                throw material_error(
                    source, "properties", "property names cannot be empty");
            }
            if(!names.insert(property_name).second) {
                throw material_error(
                    source, "properties", "duplicate property '" + property_name + "'");
            }

            const std::string property_location = "properties." + property_name;
            const YAML::Node property = entry.second;
            require_map(property, source, property_location);

            const std::string type = read_scalar<std::string>(
                required_child(property, "type", source, property_location), source,
                property_location + ".type", "a string");
            if(type == "scalar" || type == "vector") {
                validate_keys(property, {"type", "value"}, source, property_location);
                const auto value =
                    required_child(property, "value", source, property_location);
                if(type == "scalar") {
                    data.scalar_properties.emplace(property_name,
                        read_scalar<float>(value, source, property_location + ".value",
                            "a finite scalar"));
                } else {
                    if(!value.IsSequence() || value.size() != 4) {
                        throw material_error(source, property_location + ".value",
                            "expected four components");
                    }
                    std::array<float, 4> components;
                    for(std::size_t index = 0; index < components.size(); ++index) {
                        components[index] = read_scalar<float>(value[index], source,
                            property_location + ".value[" + std::to_string(index) + "]",
                            "a finite scalar");
                    }
                    data.vector_properties.emplace(property_name, components);
                }
                continue;
            } else if(type != "texture") {
                throw material_error(source, property_location + ".type",
                    "unsupported property type '" + type + "'");
            }

            validate_keys(property, {"type", "asset"}, source, property_location);

            const std::uint64_t asset = read_scalar<std::uint64_t>(
                required_child(property, "asset", source, property_location), source,
                property_location + ".asset", "a non-zero unsigned integer");
            const AssetHandle texture_handle(asset);
            if(!texture_handle) {
                throw material_error(source, property_location + ".asset",
                    "expected a non-zero unsigned integer");
            }
            data.texture_properties.emplace(property_name, texture_handle);
        }

        validate_material_data(data, source);
        return data;
    }

    void MaterialSerializer::save(
        const MaterialData& data, const std::filesystem::path& path) const {
        const std::string contents = serialize(data);
        write_text_file_atomic(path, contents);
    }

    MaterialData MaterialSerializer::load(
        const std::filesystem::path& source_path) const {
        return deserialize(read_text_file(source_path), source_path.string());
    }
}
