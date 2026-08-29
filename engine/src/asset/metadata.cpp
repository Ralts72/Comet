#include "asset/metadata.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace Comet {
    namespace {
        std::runtime_error metadata_error(
            const std::string_view source,
            const std::string_view location,
            const std::string& detail) {
            return std::runtime_error(
                "Invalid asset metadata '" + std::string(source) + "' at '"
                + std::string(location) + "': " + detail);
        }

        void require_map(
            const YAML::Node& node,
            const std::string_view source,
            const std::string_view location) {
            if(!node.IsMap()) {
                throw metadata_error(source, location, "expected a mapping");
            }
        }

        void validate_keys(const YAML::Node& root, const std::string_view source) {
            const std::unordered_set<std::string> supported{
                "version", "guid", "type"
            };
            std::unordered_set<std::string> found;
            for(const auto& entry: root) {
                if(!entry.first.IsScalar()) {
                    throw metadata_error(source, "<root>", "expected string keys");
                }

                const std::string key = entry.first.as<std::string>();
                if(!supported.contains(key)) {
                    throw metadata_error(
                        source, "<root>", "unknown field '" + key + "'");
                }
                if(!found.insert(key).second) {
                    throw metadata_error(
                        source, "<root>", "duplicate field '" + key + "'");
                }
            }
        }

        YAML::Node required_child(
            const YAML::Node& root,
            const char* key,
            const std::string_view source) {
            const YAML::Node child = root[key];
            if(!child.IsDefined()) {
                throw metadata_error(
                    source, "<root>", "missing required field '" + std::string(key) + "'");
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
                throw metadata_error(
                    source, location, "expected " + std::string(expected));
            }

            try {
                return node.as<T>();
            } catch(const YAML::Exception&) {
                throw metadata_error(
                    source, location, "expected " + std::string(expected));
            }
        }
    }

    std::string_view to_string(const AssetType type) noexcept {
        switch(type) {
            case AssetType::Texture: return "texture";
            case AssetType::Material: return "material";
            case AssetType::Mesh: return "mesh";
            case AssetType::Shader: return "shader";
            case AssetType::Scene: return "scene";
            case AssetType::Unknown: return "unknown";
        }
        return "unknown";
    }

    std::optional<AssetType> asset_type_from_string(
        const std::string_view value) noexcept {
        if(value == "texture") return AssetType::Texture;
        if(value == "material") return AssetType::Material;
        if(value == "mesh") return AssetType::Mesh;
        if(value == "shader") return AssetType::Shader;
        if(value == "scene") return AssetType::Scene;
        return std::nullopt;
    }

    std::string AssetMetadataSerializer::serialize(
        const AssetMetadata& metadata) const {
        if(!metadata.handle) {
            throw metadata_error("<memory>", "guid", "expected a non-zero value");
        }
        if(metadata.type == AssetType::Unknown) {
            throw metadata_error("<memory>", "type", "expected a known asset type");
        }

        YAML::Node root(YAML::NodeType::Map);
        root["version"] = FORMAT_VERSION;
        root["guid"] = metadata.handle.value();
        root["type"] = std::string(to_string(metadata.type));

        YAML::Emitter emitter;
        emitter << root;
        if(!emitter.good()) {
            throw std::runtime_error(
                "Failed to serialize asset metadata: "
                + emitter.GetLastError());
        }
        return std::string(emitter.c_str()) + '\n';
    }

    AssetMetadata AssetMetadataSerializer::deserialize(
        const std::string_view contents,
        const std::string_view source) const {
        YAML::Node root;
        try {
            root = YAML::Load(std::string(contents));
        } catch(const YAML::Exception& error) {
            throw std::runtime_error(
                "Failed to parse asset metadata '" + std::string(source)
                + "': " + error.what());
        }

        require_map(root, source, "<root>");
        validate_keys(root, source);

        const std::uint32_t version = read_scalar<std::uint32_t>(
            required_child(root, "version", source),
            source,
            "version",
            "an unsigned integer");
        if(version != FORMAT_VERSION) {
            throw metadata_error(
                source,
                "version",
                "unsupported version " + std::to_string(version)
                + "; expected " + std::to_string(FORMAT_VERSION));
        }

        const AssetHandle handle(read_scalar<AssetHandle::ValueType>(
            required_child(root, "guid", source),
            source,
            "guid",
            "a non-zero unsigned integer"));
        if(!handle) {
            throw metadata_error(source, "guid", "expected a non-zero value");
        }

        const std::string type_name = read_scalar<std::string>(
            required_child(root, "type", source),
            source,
            "type",
            "an asset type string");
        const auto type = asset_type_from_string(type_name);
        if(!type) {
            throw metadata_error(
                source, "type", "unknown asset type '" + type_name + "'");
        }

        return AssetMetadata{.handle = handle, .type = *type};
    }

    void AssetMetadataSerializer::save(
        const AssetMetadata& metadata,
        const std::filesystem::path& path) const {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if(!output) {
            throw std::runtime_error(
                "Failed to open asset metadata for writing: " + path.string());
        }
        output << serialize(metadata);
        if(!output) {
            throw std::runtime_error(
                "Failed to write asset metadata: " + path.string());
        }
    }

    AssetMetadata AssetMetadataSerializer::load(
        const std::filesystem::path& path) const {
        std::ifstream input(path, std::ios::binary);
        if(!input) {
            throw std::runtime_error(
                "Failed to open asset metadata: " + path.string());
        }

        std::ostringstream contents;
        contents << input.rdbuf();
        if(input.bad()) {
            throw std::runtime_error(
                "Failed to read asset metadata: " + path.string());
        }
        return deserialize(contents.str(), path.string());
    }

    std::filesystem::path metadata_path(
        const std::filesystem::path& asset_path) {
        std::filesystem::path result = asset_path;
        result += ".meta";
        return result;
    }
}
