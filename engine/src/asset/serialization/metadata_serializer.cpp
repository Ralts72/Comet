#include "asset/serialization/metadata_serializer.h"
#include "asset/serialization/yaml_serialization.h"

#include <utility>

namespace Comet {
    namespace {

        AssetResult<YAML::Node> encode_metadata(const AssetMetadata& metadata,
            const AssetSerialization::YamlContext& context) {
            if(!metadata.handle) {
                return AssetResult<YAML::Node>::failure(
                    context.error("guid", "expected a non-zero value"));
            }
            if(metadata.type == AssetType::Unknown) {
                return AssetResult<YAML::Node>::failure(
                    context.error("type", "expected a known asset type"));
            }

            const auto* texture_settings =
                std::get_if<TextureImportSettings>(&metadata.import_settings);
            if(metadata.type == AssetType::Texture && !texture_settings) {
                return AssetResult<YAML::Node>::failure(context.error(
                    "importer", "expected texture import settings for a texture asset"));
            }
            if(metadata.type != AssetType::Texture
                && !std::holds_alternative<std::monostate>(metadata.import_settings)) {
                return AssetResult<YAML::Node>::failure(context.error(
                    "importer", "import settings do not match asset type '"
                                    + std::string(to_string(metadata.type)) + "'"));
            }

            YAML::Node root(YAML::NodeType::Map);
            root["version"] = MetadataSerializer::FORMAT_VERSION;
            root["guid"] = metadata.handle.value();
            root["type"] = std::string(to_string(metadata.type));
            if(texture_settings) {
                YAML::Node importer(YAML::NodeType::Map);
                importer["color_space"] =
                    std::string(to_string(texture_settings->color_space));
                importer["flip_y"] = texture_settings->flip_y;
                root["importer"] = importer;
            }

            return AssetResult<YAML::Node>::success(std::move(root));
        }

        AssetResult<AssetMetadata> decode_metadata(
            const YAML::Node& root, const AssetSerialization::YamlContext& context) {
            context.validate_keys(root, {"version", "guid", "type", "importer"});

            const std::uint32_t version = context.read_scalar<std::uint32_t>(
                context.required_child(root, "version"), "version",
                "an unsigned integer");
            if(version != MetadataSerializer::FORMAT_VERSION) {
                return AssetResult<AssetMetadata>::failure(context.error("version",
                    "unsupported version " + std::to_string(version) + "; expected "
                        + std::to_string(MetadataSerializer::FORMAT_VERSION)));
            }

            const AssetHandle handle(context.read_scalar<AssetHandle::ValueType>(
                context.required_child(root, "guid"), "guid",
                "a non-zero unsigned integer"));
            if(!handle) {
                return AssetResult<AssetMetadata>::failure(
                    context.error("guid", "expected a non-zero value"));
            }

            const std::string type_name = context.read_scalar<std::string>(
                context.required_child(root, "type"), "type", "an asset type string");
            const auto type = asset_type_from_string(type_name);
            if(!type) {
                return AssetResult<AssetMetadata>::failure(
                    context.error("type", "unknown asset type '" + type_name + "'"));
            }

            AssetImportSettings import_settings = std::monostate{};
            const YAML::Node importer = root["importer"];
            if(*type == AssetType::Texture) {
                if(!importer.IsDefined()) {
                    return AssetResult<AssetMetadata>::failure(
                        context.error("<root>", "missing required field 'importer'"));
                }
                context.validate_keys(importer, {"color_space", "flip_y"}, "importer");

                const std::string color_space_name = context.read_scalar<std::string>(
                    context.required_child(importer, "color_space", "importer"),
                    "importer.color_space", "a texture color space string");
                const auto color_space =
                    texture_color_space_from_string(color_space_name);
                if(!color_space) {
                    return AssetResult<AssetMetadata>::failure(
                        context.error("importer.color_space",
                            "unknown texture color space '" + color_space_name + "'"));
                }

                import_settings = TextureImportSettings{.color_space = *color_space,
                    .flip_y = context.read_scalar<bool>(
                        context.required_child(importer, "flip_y", "importer"),
                        "importer.flip_y", "a boolean")};
            } else if(importer.IsDefined()) {
                return AssetResult<AssetMetadata>::failure(context.error(
                    "importer", "import settings are not supported for asset type '"
                                    + std::string(to_string(*type)) + "'"));
            }

            return AssetResult<AssetMetadata>::success(AssetMetadata{.handle = handle,
                .type = *type,
                .import_settings = std::move(import_settings)});
        }
    }

    AssetResult<std::string> MetadataSerializer::serialize(
        const AssetMetadata& metadata) const {
        return AssetSerialization::serialize_yaml(
            "asset metadata", metadata, encode_metadata);
    }

    AssetResult<AssetMetadata> MetadataSerializer::deserialize(
        const std::string_view contents, const std::string_view source) const {
        return AssetSerialization::deserialize_yaml<AssetMetadata>(
            "asset metadata", contents, source, decode_metadata);
    }

    AssetResult<void> MetadataSerializer::save(
        const AssetMetadata& metadata, const std::filesystem::path& path) const {
        return AssetSerialization::save(*this, metadata, path);
    }

    AssetResult<AssetMetadata> MetadataSerializer::load(
        const std::filesystem::path& path) const {
        return AssetSerialization::load(*this, path);
    }
}
