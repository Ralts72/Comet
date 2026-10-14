#include "asset/serialization/metadata_serializer.h"
#include "common/json.h"

#include <utility>
#include <unordered_set>

namespace Comet {
    namespace {

        Result<void> validate_subresources(
            const AssetMetadata& metadata, const Json::Context& context) {
            if(!metadata.subresources.empty() && metadata.type != AssetType::Model)
                return Result<void>::failure(
                    context.error("subresources", "only models own subresources"));
            std::unordered_set<std::string> keys;
            std::unordered_set<AssetHandle> handles{metadata.handle};
            for(const auto& subresource : metadata.subresources) {
                if(subresource.key.empty() || !keys.insert(subresource.key).second
                    || !subresource.handle || !handles.insert(subresource.handle).second)
                    return Result<void>::failure(
                        context.error("subresources", "keys and non-zero handles must be unique"));
                if(subresource.type != AssetType::Mesh && subresource.type != AssetType::Material
                    && subresource.type != AssetType::Texture)
                    return Result<void>::failure(
                        context.error("subresources", "expected a mesh, material or texture"));
            }
            return Result<void>::success();
        }

        Result<void> encode_metadata(
            const AssetMetadata& metadata, const Json::Context& context, Json::Writer& writer) {
            if(auto valid = validate_subresources(metadata, context); !valid)
                return valid;
            if(!metadata.handle) {
                return Result<void>::failure(context.error("guid", "expected a non-zero value"));
            }
            if(metadata.type == AssetType::Unknown) {
                return Result<void>::failure(context.error("type", "expected a known asset type"));
            }

            const auto* texture_settings =
                std::get_if<TextureImportSettings>(&metadata.import_settings);
            const auto* audio_settings =
                std::get_if<AudioImportSettings>(&metadata.import_settings);
            if(metadata.type == AssetType::Texture && !texture_settings) {
                return Result<void>::failure(context.error(
                    "importer", "expected texture import settings for a texture asset"));
            }
            if((texture_settings && metadata.type != AssetType::Texture)
                || (audio_settings && metadata.type != AssetType::Audio)) {
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
                writer.field("generate_mipmaps", texture_settings->generate_mipmaps);
                writer.field("normal_map", texture_settings->normal_map);
                writer.end_object();
            }
            if(audio_settings) {
                writer.key("importer");
                writer.begin_object();
                writer.field("stream", audio_settings->stream);
                writer.end_object();
            }
            if(!metadata.subresources.empty()) {
                writer.key("subresources");
                writer.begin_array();
                for(const auto& resource : metadata.subresources) {
                    writer.begin_object();
                    writer.field("key", resource.key);
                    writer.field("guid", resource.handle.value());
                    writer.field("type", to_string(resource.type));
                    writer.end_object();
                }
                writer.end_array();
            }
            writer.end_object();

            return Result<void>::success();
        }

        Result<AssetMetadata> decode_metadata(
            const Json::Node& root, const Json::Context& context) {
            if(auto valid = context.validate_keys(
                   root, {"version", "guid", "type", "importer", "subresources"});
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
                if(auto valid = context.validate_keys(importer,
                       {"color_space", "flip_y", "generate_mipmaps", "normal_map"}, "importer");
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
                const auto mipmaps = context.read_optional_field<bool>(
                    importer, "generate_mipmaps", "a boolean", "importer");
                if(!mipmaps)
                    return Result<AssetMetadata>::failure(mipmaps.error());
                const auto normal_map = context.read_optional_field<bool>(
                    importer, "normal_map", "a boolean", "importer");
                if(!normal_map)
                    return Result<AssetMetadata>::failure(normal_map.error());
                import_settings = TextureImportSettings{.color_space = *color_space,
                    .flip_y = flip_y.value(),
                    .generate_mipmaps = mipmaps.value().value_or(true),
                    .normal_map = normal_map.value().value_or(false)};
            } else if(*type == AssetType::Audio && has_importer) {
                if(auto valid = context.validate_keys(importer, {"stream"}, "importer"); !valid)
                    return Result<AssetMetadata>::failure(valid.error());
                auto stream = context.read_field<bool>(importer, "stream", "a boolean", "importer");
                if(!stream)
                    return Result<AssetMetadata>::failure(stream.error());
                import_settings = AudioImportSettings{stream.value()};
            } else if(has_importer) {
                return Result<AssetMetadata>::failure(
                    context.error("importer", "import settings are not supported for asset type '"
                                                  + std::string(to_string(*type)) + "'"));
            }

            AssetMetadata result{
                .handle = handle, .type = *type, .import_settings = std::move(import_settings)};
            Json::Node children;
            if(!root["subresources"].get(children)) {
                auto array = context.array(children, "subresources");
                if(!array)
                    return Result<AssetMetadata>::failure(array.error());
                for(const auto child : array.value()) {
                    if(auto valid =
                            context.validate_keys(child, {"key", "guid", "type"}, "subresources");
                        !valid)
                        return Result<AssetMetadata>::failure(valid.error());
                    const auto key = context.read_field<std::string>(
                        child, "key", "a source key", "subresources");
                    const auto id = context.read_field<uint64_t>(
                        child, "guid", "an asset handle", "subresources");
                    const auto type_name = context.read_field<std::string>(
                        child, "type", "an asset type", "subresources");
                    if(!key)
                        return Result<AssetMetadata>::failure(key.error());
                    if(!id)
                        return Result<AssetMetadata>::failure(id.error());
                    if(!type_name)
                        return Result<AssetMetadata>::failure(type_name.error());
                    const auto child_type = asset_type_from_string(type_name.value());
                    if(!child_type)
                        return Result<AssetMetadata>::failure(
                            context.error("subresources.type", "unknown asset type"));
                    result.subresources.push_back(
                        {key.value(), AssetHandle{id.value()}, *child_type});
                }
            }
            if(auto valid = validate_subresources(result, context); !valid)
                return Result<AssetMetadata>::failure(valid.error());
            return Result<AssetMetadata>::success(std::move(result));
        }
    }

    Result<std::string> MetadataSerializer::serialize(const AssetMetadata& metadata) const {
        return Json::serialize("asset metadata", metadata, encode_metadata);
    }

    Result<AssetMetadata> MetadataSerializer::deserialize(
        const std::string_view contents, const std::string_view source) const {
        return Json::deserialize<AssetMetadata>(
            "asset metadata", contents, source, decode_metadata);
    }

    Result<void> MetadataSerializer::save(
        const AssetMetadata& metadata, const std::filesystem::path& path) const {
        return Json::save(*this, metadata, path);
    }

    Result<AssetMetadata> MetadataSerializer::load(const std::filesystem::path& path) const {
        return Json::load(*this, path);
    }
}
