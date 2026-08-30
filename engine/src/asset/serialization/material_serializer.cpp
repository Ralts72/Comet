#include "asset/serialization/material_serializer.h"
#include "common/file_io.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Comet {
    namespace {
        std::runtime_error material_error(
            const std::string_view source,
            const std::string_view location,
            const std::string& detail) {
            return std::runtime_error(
                "Invalid material '" + std::string(source) + "' at '"
                + std::string(location) + "': " + detail);
        }

        void require_map(
            const YAML::Node& node,
            const std::string_view source,
            const std::string_view location) {
            if(!node.IsMap()) {
                throw material_error(source, location, "expected a mapping");
            }
        }

        void validate_keys(
            const YAML::Node& node,
            const std::unordered_set<std::string>& supported,
            const std::string_view source,
            const std::string_view location) {
            std::unordered_set<std::string> found;
            for(const auto& entry: node) {
                if(!entry.first.IsScalar()) {
                    throw material_error(source, location, "expected string keys");
                }

                const std::string key = entry.first.as<std::string>();
                if(!supported.contains(key)) {
                    throw material_error(
                        source, location, "unknown field '" + key + "'");
                }
                if(!found.insert(key).second) {
                    throw material_error(
                        source, location, "duplicate field '" + key + "'");
                }
            }
        }

        YAML::Node required_child(
            const YAML::Node& node,
            const char* key,
            const std::string_view source,
            const std::string_view location) {
            const YAML::Node child = node[key];
            if(!child.IsDefined()) {
                throw material_error(
                    source,
                    location,
                    "missing required field '" + std::string(key) + "'");
            }
            return child;
        }

        template<typename T>
        T read_scalar(
            const YAML::Node& node,
            const std::string_view source,
            const std::string_view location,
            const std::string_view expected) {
            if(!node.IsScalar()) {
                throw material_error(
                    source, location, "expected " + std::string(expected));
            }

            try {
                return node.as<T>();
            } catch(const YAML::Exception&) {
                throw material_error(
                    source, location, "expected " + std::string(expected));
            }
        }

        void validate_material_data(
            const MaterialData& data,
            const std::string_view source) {
            if(data.template_name.empty()) {
                throw material_error(
                    source, "template", "expected a non-empty string");
            }
            for(const auto& [property_name, texture_handle]:
                data.texture_properties) {
                if(property_name.empty()) {
                    throw material_error(
                        source,
                        "properties",
                        "property names cannot be empty");
                }
                if(!texture_handle) {
                    throw material_error(
                        source,
                        "properties." + property_name + ".asset",
                        "expected a non-zero unsigned integer");
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
        for(const auto& [property_name, texture_handle]:
            data.texture_properties) {
            YAML::Node property(YAML::NodeType::Map);
            property["type"] = "texture";
            property["asset"] = texture_handle.value();
            properties[property_name] = property;
        }
        root["properties"] = properties;

        YAML::Emitter emitter;
        emitter << root;
        if(!emitter.good()) {
            throw std::runtime_error(
                "Failed to serialize material: "
                + emitter.GetLastError());
        }
        return std::string(emitter.c_str()) + '\n';
    }

    MaterialData MaterialSerializer::deserialize(
        const std::string_view contents,
        const std::string_view source) const {
        YAML::Node root;
        try {
            root = YAML::Load(std::string(contents));
        } catch(const YAML::Exception& exception) {
            throw material_error(source, "<yaml>", exception.what());
        }

        require_map(root, source, "<root>");
        validate_keys(
            root,
            {"version", "template", "properties"},
            source,
            "<root>");

        const std::uint32_t version = read_scalar<std::uint32_t>(
            required_child(root, "version", source, "<root>"),
            source,
            "version",
            "an unsigned integer");
        if(version != FORMAT_VERSION) {
            throw material_error(
                source,
                "version",
                "unsupported version " + std::to_string(version));
        }

        MaterialData data;
        data.template_name = read_scalar<std::string>(
            required_child(root, "template", source, "<root>"),
            source,
            "template",
            "a non-empty string");
        if(data.template_name.empty()) {
            throw material_error(source, "template", "expected a non-empty string");
        }

        const YAML::Node properties =
                required_child(root, "properties", source, "<root>");
        require_map(properties, source, "properties");
        for(const auto& entry: properties) {
            if(!entry.first.IsScalar()) {
                throw material_error(source, "properties", "expected string keys");
            }

            const std::string property_name = entry.first.as<std::string>();
            if(property_name.empty()) {
                throw material_error(
                    source, "properties", "property names cannot be empty");
            }
            if(data.texture_properties.contains(property_name)) {
                throw material_error(
                    source,
                    "properties",
                    "duplicate property '" + property_name + "'");
            }

            const std::string property_location =
                    "properties." + property_name;
            const YAML::Node property = entry.second;
            require_map(property, source, property_location);
            validate_keys(
                property,
                {"type", "asset"},
                source,
                property_location);

            const std::string type = read_scalar<std::string>(
                required_child(
                    property, "type", source, property_location),
                source,
                property_location + ".type",
                "a string");
            if(type != "texture") {
                throw material_error(
                    source,
                    property_location + ".type",
                    "unsupported property type '" + type + "'");
            }

            const std::uint64_t asset = read_scalar<std::uint64_t>(
                required_child(
                    property, "asset", source, property_location),
                source,
                property_location + ".asset",
                "a non-zero unsigned integer");
            const AssetHandle texture_handle(asset);
            if(!texture_handle) {
                throw material_error(
                    source,
                    property_location + ".asset",
                    "expected a non-zero unsigned integer");
            }
            data.texture_properties.emplace(property_name, texture_handle);
        }

        return data;
    }

    void MaterialSerializer::save(
        const MaterialData& data,
        const std::filesystem::path& path) const {
        const std::string contents = serialize(data);
        write_text_file_atomic(path, contents);
    }

    MaterialData MaterialSerializer::load(
        const std::filesystem::path& source_path) const {
        std::ifstream input(source_path, std::ios::binary);
        if(!input) {
            throw std::runtime_error(
                "Failed to open material '" + source_path.string() + "'");
        }

        std::ostringstream contents;
        contents << input.rdbuf();
        if(input.bad()) {
            throw std::runtime_error(
                "Failed to read material '" + source_path.string() + "'");
        }
        return deserialize(contents.str(), source_path.string());
    }
}
