#include "asset/product_index.h"
#include "asset/default_assets.h"
#include "common/json.h"
#include <array>
#include <unordered_set>

namespace Comet {
    namespace {
        constexpr uint64_t VERSION = 1;
        constexpr std::size_t MAX_BYTES = 32 * 1024 * 1024;
        constexpr std::size_t MAX_ASSETS = 100000;

        bool valid_path(const std::filesystem::path& path) {
            const auto text = path.generic_string();
            return !path.empty() && !path.is_absolute() && !path.has_root_name()
                   && text.find_first_of(":\\") == std::string::npos
                   && text.find('\0') == std::string::npos
                   && std::ranges::none_of(path, [](const auto& part) {
                          return part == ".." || part == "." || part.empty();
                      });
        }
        bool valid_sampler(const SamplerDesc& value) {
            const auto filter = [](Filter f) {
                return f == Filter::Nearest || f == Filter::Linear;
            };
            const auto wrap = [](SamplerAddressMode m) {
                return m == SamplerAddressMode::Repeat || m == SamplerAddressMode::MirrorRepeat
                       || m == SamplerAddressMode::ClampToEdge;
            };
            return filter(value.min_filter) && filter(value.mag_filter)
                   && (value.mipmap_mode == SamplerMipmapMode::Nearest
                       || value.mipmap_mode == SamplerMipmapMode::Linear)
                   && wrap(value.address_mode_u) && wrap(value.address_mode_v)
                   && wrap(value.address_mode_w) && std::isfinite(value.max_anisotropy)
                   && value.max_anisotropy >= 1;
        }
        std::array<uint64_t, 6> sampler_values(const SamplerDesc& value) {
            return {uint64_t(value.mag_filter), uint64_t(value.min_filter),
                uint64_t(value.mipmap_mode), uint64_t(value.address_mode_u),
                uint64_t(value.address_mode_v), uint64_t(value.address_mode_w)};
        }
        Result<SamplerDesc> read_sampler(Json::Node node, const Json::Context& context) {
            using Read = Result<SamplerDesc>;
            if(auto keys = context.validate_keys(node, {"modes", "anisotropy"}, "sampler"); !keys)
                return Read::failure(keys.error());
            auto modes_node = context.required_child(node, "modes", "sampler");
            if(!modes_node)
                return Read::failure(modes_node.error());
            auto modes = context.array(modes_node.value(), "sampler.modes");
            if(!modes || modes.value().size() != 6)
                return Read::failure(context.error("sampler.modes", "expected six mode values"));
            std::array<uint32_t, 6> values{};
            size_t index = 0;
            for(auto mode : modes.value()) {
                auto value =
                    context.read_scalar<uint32_t>(mode, "sampler.modes", "an unsigned integer");
                if(!value)
                    return Read::failure(value.error());
                // 枚举转换前限制数值，避免底层小整数类型截断后接受非法值。
                if(value.value() > 2)
                    return Read::failure("Invalid sampler mode");
                values[index++] = value.value();
            }
            auto anisotropy = context.read_field<float>(node, "anisotropy", "a finite number");
            if(!anisotropy)
                return Read::failure(anisotropy.error());
            SamplerDesc result{Filter(values[0]), Filter(values[1]), SamplerMipmapMode(values[2]),
                SamplerAddressMode(values[3]), SamplerAddressMode(values[4]),
                SamplerAddressMode(values[5]), anisotropy.value()};
            if(!valid_sampler(result))
                return Read::failure("Invalid product sampler");
            return Read::success(result);
        }
    }

    Result<void> ProductIndex::validate() const {
        if(assets.size() > MAX_ASSETS)
            return Result<void>::failure("Too many product assets");
        std::unordered_set<AssetHandle> handles;
        std::unordered_set<std::filesystem::path> paths;
        for(const auto& asset : assets) {
            if(!asset.handle || !handles.insert(asset.handle).second)
                return Result<void>::failure("Invalid or duplicate product asset handle");
            if(!valid_path(asset.path) || !paths.insert(asset.path).second)
                return Result<void>::failure("Invalid or duplicate product asset path");
            if(asset.type == AssetType::Unknown || asset.type == AssetType::Shader
                || asset.type == AssetType::Model || !asset_type_from_string(to_string(asset.type))
                || to_string(asset.type) == "unknown")
                return Result<void>::failure("Product index contains a source-only asset type");
            if(const auto* builtin = find_default_asset(asset.handle);
                builtin
                && (builtin->type != asset.type
                    || (asset.type != AssetType::Mesh
                        && asset.type != AssetType::AnimationController)))
                return Result<void>::failure("Product redefines an embedded default asset");
            if(asset.sampler
                && (asset.type != AssetType::Texture || !valid_sampler(*asset.sampler)))
                return Result<void>::failure("Invalid product texture sampler");
            if(asset.audio_stream && asset.type != AssetType::Audio)
                return Result<void>::failure("Only audio products support streaming");
        }
        for(const auto& asset : assets) {
            std::unordered_set<AssetHandle> dependencies;
            for(const auto dependency : asset.dependencies) {
                const auto* builtin = find_default_asset(dependency);
                if(!dependency || !dependencies.insert(dependency).second
                    || (!handles.contains(dependency)
                        && !(builtin && builtin->type == AssetType::Material)))
                    return Result<void>::failure("Missing or duplicate product dependency");
            }
        }
        return Result<void>::success();
    }

    Result<void> ProductIndex::save(const std::filesystem::path& path) const {
        if(auto valid = validate(); !valid)
            return valid;
        Json::Writer json;
        json.begin_object();
        json.field("version", VERSION);
        json.key("assets");
        json.begin_array();
        for(const auto& asset : assets) {
            json.begin_object();
            json.field("handle", asset.handle.value());
            json.field("type", to_string(asset.type));
            json.field("path", asset.path.generic_string());
            if(asset.audio_stream)
                json.field("audio_stream", true);
            json.key("dependencies");
            json.begin_array();
            for(const auto dependency : asset.dependencies)
                json.value(dependency.value());
            json.end_array();
            if(asset.sampler) {
                json.key("sampler");
                json.begin_object();
                json.key("modes");
                json.begin_array();
                for(auto value : sampler_values(*asset.sampler))
                    json.value(value);
                json.end_array();
                json.field("anisotropy", asset.sampler->max_anisotropy);
                json.end_object();
            }
            json.end_object();
        }
        json.end_array();
        json.end_object();
        auto contents = std::move(json).finish();
        if(!contents)
            return Result<void>::failure(contents.error());
        if(contents.value().size() > MAX_BYTES)
            return Result<void>::failure("Product index is too large");
        return write_text_file_atomic(path, contents.value());
    }

    Result<ProductIndex> ProductIndex::load(const std::filesystem::path& path) {
        using Read = Result<ProductIndex>;
        auto bytes = read_binary_file(path, MAX_BYTES);
        if(!bytes)
            return Read::failure(bytes.error());
        const auto source = path.generic_string();
        const Json::Context context("product index", source);
        simdjson::dom::parser parser;
        auto document = context.parse(
            parser, std::string_view(
                        reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()));
        if(!document)
            return Read::failure(document.error());
        if(auto keys = context.validate_keys(document.value(), {"version", "assets"}); !keys)
            return Read::failure(keys.error());
        const auto version =
            context.read_field<uint64_t>(document.value(), "version", "an unsigned integer");
        if(!version || version.value() != VERSION)
            return Read::failure("Unsupported product index version");
        auto array_node = context.required_child(document.value(), "assets");
        if(!array_node)
            return Read::failure(array_node.error());
        auto array = context.array(array_node.value(), "assets");
        if(!array)
            return Read::failure(array.error());
        if(array.value().size() > MAX_ASSETS)
            return Read::failure("Too many product assets");
        ProductIndex result;
        for(auto entry : array.value()) {
            if(auto keys = context.validate_keys(
                   entry, {"handle", "type", "path", "dependencies", "sampler", "audio_stream"});
                !keys)
                return Read::failure(keys.error());
            auto handle = context.read_field<uint64_t>(entry, "handle", "an unsigned integer");
            auto type_name = context.read_field<std::string>(entry, "type", "a string");
            auto file = context.read_field<std::string>(entry, "path", "a string");
            if(!handle)
                return Read::failure(handle.error());
            if(!type_name)
                return Read::failure(type_name.error());
            if(!file)
                return Read::failure(file.error());
            const auto type = asset_type_from_string(type_name.value());
            if(!type)
                return Read::failure("Unknown product asset type");
            AssetProduct asset{AssetHandle{handle.value()}, *type, file.value()};
            auto stream = context.read_optional_field<bool>(entry, "audio_stream", "a boolean");
            if(!stream)
                return Read::failure(stream.error());
            asset.audio_stream = stream.value().value_or(false);
            auto dependencies_node = context.required_child(entry, "dependencies");
            if(!dependencies_node)
                return Read::failure(dependencies_node.error());
            auto dependencies = context.array(dependencies_node.value(), "dependencies");
            if(!dependencies)
                return Read::failure(dependencies.error());
            for(auto item : dependencies.value()) {
                auto dependency =
                    context.read_scalar<uint64_t>(item, "dependencies", "an unsigned integer");
                if(!dependency)
                    return Read::failure(dependency.error());
                asset.dependencies.emplace_back(dependency.value());
            }
            auto sampler_node = context.find(entry, "sampler");
            if(!sampler_node)
                return Read::failure(sampler_node.error());
            if(sampler_node.value()) {
                auto sampler = read_sampler(*sampler_node.value(), context);
                if(!sampler)
                    return Read::failure(sampler.error());
                asset.sampler = sampler.value();
            }
            result.assets.push_back(std::move(asset));
        }
        if(auto valid = result.validate(); !valid)
            return Read::failure(valid.error());
        return Read::success(std::move(result));
    }
}
