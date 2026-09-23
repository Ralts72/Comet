#include "asset/serialization/shader_program_serializer.h"
#include "asset/serialization/json_serialization.h"

#include <utility>

namespace Comet {
    namespace {
        Result<void> validate_stage(const ShaderProgramStage& stage, const Json::Context& context,
            const std::string_view name) {
            if(!stage.source)
                return Result<void>::failure(
                    context.error(name, "source must be a non-zero handle"));
            if(stage.entry.empty())
                return Result<void>::failure(context.error(name, "entry must not be empty"));
            return Result<void>::success();
        }

        Result<void> encode(
            const ShaderProgramData& data, const Json::Context& context, Json::Writer& writer) {
            if(auto valid = validate_stage(data.vertex, context, "vertex"); !valid)
                return valid;
            if(auto valid = validate_stage(data.fragment, context, "fragment"); !valid)
                return valid;
            const auto write_stage = [&](std::string_view name, const ShaderProgramStage& stage) {
                writer.key(name);
                writer.begin_object();
                writer.field("source", stage.source.value());
                writer.field("entry", stage.entry);
                writer.end_object();
            };
            writer.begin_object();
            writer.field("version", std::uint64_t(ShaderProgramSerializer::FORMAT_VERSION));
            write_stage("vertex", data.vertex);
            write_stage("fragment", data.fragment);
            writer.end_object();
            return Result<void>::success();
        }

        Result<ShaderProgramStage> read_stage(
            const Json::Node root, const Json::Context& context, const std::string_view name) {
            auto node = context.required_child(root, name);
            if(!node)
                return Result<ShaderProgramStage>::failure(node.error());
            if(auto valid = context.validate_keys(node.value(), {"source", "entry"}, name); !valid)
                return Result<ShaderProgramStage>::failure(valid.error());
            auto source = context.read_field<std::uint64_t>(
                node.value(), "source", "a non-zero handle", name);
            if(!source)
                return Result<ShaderProgramStage>::failure(source.error());
            auto entry = context.read_field<std::string>(node.value(), "entry", "a string", name);
            if(!entry)
                return Result<ShaderProgramStage>::failure(entry.error());
            ShaderProgramStage stage{AssetHandle(source.value()), std::move(entry).value()};
            if(auto valid = validate_stage(stage, context, name); !valid)
                return Result<ShaderProgramStage>::failure(valid.error());
            return Result<ShaderProgramStage>::success(std::move(stage));
        }

        Result<ShaderProgramData> decode(const Json::Node root, const Json::Context& context) {
            if(auto valid = context.validate_keys(root, {"version", "vertex", "fragment"}); !valid)
                return Result<ShaderProgramData>::failure(valid.error());
            auto version =
                context.read_field<std::uint32_t>(root, "version", "an unsigned integer");
            if(!version)
                return Result<ShaderProgramData>::failure(version.error());
            if(version.value() != ShaderProgramSerializer::FORMAT_VERSION)
                return Result<ShaderProgramData>::failure(
                    context.error("version", "unsupported format version"));
            auto vertex = read_stage(root, context, "vertex");
            if(!vertex)
                return Result<ShaderProgramData>::failure(vertex.error());
            auto fragment = read_stage(root, context, "fragment");
            if(!fragment)
                return Result<ShaderProgramData>::failure(fragment.error());
            return Result<ShaderProgramData>::success(
                {std::move(vertex).value(), std::move(fragment).value()});
        }
    }

    Result<std::string> ShaderProgramSerializer::serialize(const ShaderProgramData& data) const {
        return AssetSerialization::serialize_json("shader program", data, encode);
    }

    Result<ShaderProgramData> ShaderProgramSerializer::deserialize(
        const std::string_view contents, const std::string_view source) const {
        return AssetSerialization::deserialize_json<ShaderProgramData>(
            "shader program", contents, source, decode);
    }

    Result<void> ShaderProgramSerializer::save(
        const ShaderProgramData& data, const std::filesystem::path& path) const {
        return AssetSerialization::save(*this, data, path);
    }

    Result<ShaderProgramData> ShaderProgramSerializer::load(
        const std::filesystem::path& path) const {
        return AssetSerialization::load(*this, path);
    }
}
