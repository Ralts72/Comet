#include "asset/serialization/metadata_serializer.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

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
                "version", "guid", "type", "importer"
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

        void validate_texture_importer_keys(
            const YAML::Node& importer,
            const std::string_view source) {
            const std::unordered_set<std::string> supported{
                "color_space", "flip_y"
            };
            std::unordered_set<std::string> found;
            for(const auto& entry: importer) {
                if(!entry.first.IsScalar()) {
                    throw metadata_error(
                        source, "importer", "expected string keys");
                }

                const std::string key = entry.first.as<std::string>();
                if(!supported.contains(key)) {
                    throw metadata_error(
                        source, "importer", "unknown field '" + key + "'");
                }
                if(!found.insert(key).second) {
                    throw metadata_error(
                        source, "importer", "duplicate field '" + key + "'");
                }
            }
        }

        YAML::Node required_child(
            const YAML::Node& root,
            const char* key,
            const std::string_view source,
            const std::string_view parent_location = "<root>") {
            const YAML::Node child = root[key];
            if(!child.IsDefined()) {
                throw metadata_error(
                    source,
                    parent_location,
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

    std::string AssetMetadataSerializer::serialize(
        const AssetMetadata& metadata) const {
        if(!metadata.handle) {
            throw metadata_error("<memory>", "guid", "expected a non-zero value");
        }
        if(metadata.type == AssetType::Unknown) {
            throw metadata_error("<memory>", "type", "expected a known asset type");
        }

        const auto* texture_settings = std::get_if<TextureImportSettings>(
            &metadata.import_settings);
        if(metadata.type == AssetType::Texture && !texture_settings) {
            throw metadata_error(
                "<memory>",
                "importer",
                "expected texture import settings for a texture asset");
        }
        if(metadata.type != AssetType::Texture
           && !std::holds_alternative<std::monostate>(metadata.import_settings)) {
            throw metadata_error(
                "<memory>",
                "importer",
                "import settings do not match asset type '"
                + std::string(to_string(metadata.type)) + "'");
        }

        YAML::Node root(YAML::NodeType::Map);
        root["version"] = FORMAT_VERSION;
        root["guid"] = metadata.handle.value();
        root["type"] = std::string(to_string(metadata.type));
        if(texture_settings) {
            YAML::Node importer(YAML::NodeType::Map);
            importer["color_space"] = std::string(to_string(
                texture_settings->color_space));
            importer["flip_y"] = texture_settings->flip_y;
            root["importer"] = importer;
        }

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

        AssetImportSettings import_settings = std::monostate{};
        const YAML::Node importer = root["importer"];
        if(*type == AssetType::Texture) {
            if(!importer.IsDefined()) {
                throw metadata_error(
                    source,
                    "<root>",
                    "missing required field 'importer'");
            }
            require_map(importer, source, "importer");
            validate_texture_importer_keys(importer, source);

            const std::string color_space_name = read_scalar<std::string>(
                required_child(importer, "color_space", source, "importer"),
                source,
                "importer.color_space",
                "a texture color space string");
            const auto color_space = texture_color_space_from_string(
                color_space_name);
            if(!color_space) {
                throw metadata_error(
                    source,
                    "importer.color_space",
                    "unknown texture color space '" + color_space_name + "'");
            }

            import_settings = TextureImportSettings{
                .color_space = *color_space,
                .flip_y = read_scalar<bool>(
                    required_child(
                        importer, "flip_y", source, "importer"),
                    source,
                    "importer.flip_y",
                    "a boolean")
            };
        } else if(importer.IsDefined()) {
            throw metadata_error(
                source,
                "importer",
                "import settings are not supported for asset type '"
                + std::string(to_string(*type)) + "'");
        }

        return AssetMetadata{
            .handle = handle,
            .type = *type,
            .import_settings = std::move(import_settings)
        };
    }

    void AssetMetadataSerializer::save(
        const AssetMetadata& metadata,
        const std::filesystem::path& path) const {
        const std::string contents = serialize(metadata);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if(!output) {
            throw std::runtime_error(
                "Failed to open asset metadata for writing: " + path.string());
        }
        output << contents;
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
}
