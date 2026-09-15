#include "asset/serialization/metadata_serializer.h"
#include "asset/serialization/json_serialization.h"

#include <utility>

namespace Comet {
    namespace {

        Result<void> encode_metadata(
            const AssetMetadata& metadata, const Json::Context& context, Json::Writer& writer) {
            if(!metadata.handle) {
                return Result<void>::failure(context.error("guid", "expected a non-zero value"));
            }
            if(metadata.type == AssetType::Unknown) {
                return Result<void>::failure(context.error("type", "expected a known asset type"));
            }

            const auto* texture_settings =
                std::get_if<TextureImportSettings>(&metadata.import_settings);
            if(metadata.type == AssetType::Texture && !texture_settings) {
                return Result<void>::failure(context.error(
                    "importer", "expected texture import settings for a texture asset"));
            }
            if(metadata.type != AssetType::Texture
                && !std::holds_alternative<std::monostate>(metadata.import_settings)) {
                return Result<void>::failure(
                    context.error("importer", "import settings do not match asset type '"
                                                  + std::string(to_string(metadata.type)) + "'"));
            }

            writer.begin_object();
            writer.field("version", std::uint64_t(MetadataSerializer::FORMAT_VERSION));
            writer.field("guid", metadata.handle.value());
            writer.field("type", to_string(metadata.type));
            if(texture_settings) {
                writer.key("importer");
                writer.begin_object();
                writer.field("color_space", to_string(texture_settings->color_space));
                writer.field("flip_y", texture_settings->flip_y);
                writer.end_object();
            }
            writer.end_object();

            return Result<void>::success();
        }

        Result<AssetMetadata> decode_metadata(
            const Json::Node& root, const Json::Context& context) {
            if(auto valid = context.validate_keys(root, {"version", "guid", "type", "importer"});
                !valid)
                return Result<AssetMetadata>::failure(valid.error());

            const auto version =
                context.read_field<std::uint32_t>(root, "version", "an unsigned integer");
            if(!version)
                return Result<AssetMetadata>::failure(version.error());
            if(version.value() != MetadataSerializer::FORMAT_VERSION) {
                return Result<AssetMetadata>::failure(context.error("version",
                    "unsupported version " + std::to_string(version.value()) + "; expected "
                        + std::to_string(MetadataSerializer::FORMAT_VERSION)));
            }

            const auto guid = context.read_field<AssetHandle::ValueType>(
                root, "guid", "a non-zero unsigned integer");
            if(!guid)
                return Result<AssetMetadata>::failure(guid.error());
            const AssetHandle handle(guid.value());
            if(!handle) {
                return Result<AssetMetadata>::failure(
                    context.error("guid", "expected a non-zero value"));
            }

            const auto type_name =
                context.read_field<std::string>(root, "type", "an asset type string");
            if(!type_name)
                return Result<AssetMetadata>::failure(type_name.error());
            const auto type = asset_type_from_string(type_name.value());
            if(!type) {
                return Result<AssetMetadata>::failure(
                    context.error("type", "unknown asset type '" + type_name.value() + "'"));
            }

            AssetImportSettings import_settings = std::monostate{};
            Json::Node importer;
            const bool has_importer = !root["importer"].get(importer);
            if(*type == AssetType::Texture) {
                if(!has_importer) {
                    return Result<AssetMetadata>::failure(
                        context.error("<root>", "missing required field 'importer'"));
                }
                if(auto valid =
                        context.validate_keys(importer, {"color_space", "flip_y"}, "importer");
                    !valid)
                    return Result<AssetMetadata>::failure(valid.error());

                const auto color_space_name = context.read_field<std::string>(
                    importer, "color_space", "a texture color space string", "importer");
                if(!color_space_name)
                    return Result<AssetMetadata>::failure(color_space_name.error());
                const auto color_space = texture_color_space_from_string(color_space_name.value());
                if(!color_space) {
                    return Result<AssetMetadata>::failure(context.error("importer.color_space",
                        "unknown texture color space '" + color_space_name.value() + "'"));
                }

                const auto flip_y =
                    context.read_field<bool>(importer, "flip_y", "a boolean", "importer");
                if(!flip_y)
                    return Result<AssetMetadata>::failure(flip_y.error());
                import_settings =
                    TextureImportSettings{.color_space = *color_space, .flip_y = flip_y.value()};
            } else if(has_importer) {
                return Result<AssetMetadata>::failure(
                    context.error("importer", "import settings are not supported for asset type '"
                                                  + std::string(to_string(*type)) + "'"));
            }

            return Result<AssetMetadata>::success(AssetMetadata{
                .handle = handle, .type = *type, .import_settings = std::move(import_settings)});
        }
    }

    Result<std::string> MetadataSerializer::serialize(const AssetMetadata& metadata) const {
        return AssetSerialization::serialize_json("asset metadata", metadata, encode_metadata);
    }

    Result<AssetMetadata> MetadataSerializer::deserialize(
        const std::string_view contents, const std::string_view source) const {
        return AssetSerialization::deserialize_json<AssetMetadata>(
            "asset metadata", contents, source, decode_metadata);
    }

    Result<void> MetadataSerializer::save(
        const AssetMetadata& metadata, const std::filesystem::path& path) const {
        return AssetSerialization::save(*this, metadata, path);
    }

    Result<AssetMetadata> MetadataSerializer::load(const std::filesystem::path& path) const {
        return AssetSerialization::load(*this, path);
    }
}
