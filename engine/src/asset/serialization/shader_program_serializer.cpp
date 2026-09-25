#include "asset/serialization/shader_program_serializer.h"
#include "asset/serialization/json_serialization.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
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

        Result<void> validate_material(
            const ShaderProgramMaterial& material, const Json::Context& context) {
            std::unordered_set<std::string_view> names;
            const auto validate_name = [&](const std::string& name) {
                if(name.empty() || !names.insert(name).second)
                    return Result<void>::failure(
                        context.error("material", "property names must be non-empty and unique"));
                return Result<void>::success();
            };
            for(const auto& texture : material.textures)
                if(auto result = validate_name(texture.name); !result)
                    return result;
            for(const auto& scalar : material.scalars) {
                if(auto result = validate_name(scalar.name); !result)
                    return result;
                if(!std::isfinite(scalar.default_value) || !std::isfinite(scalar.min_value)
                    || !std::isfinite(scalar.max_value) || !std::isfinite(scalar.step)
                    || scalar.min_value > scalar.max_value || scalar.step <= 0)
                    return Result<void>::failure(
                        context.error("material", "invalid scalar editing range or default"));
            }
            for(const auto& vector : material.vectors) {
                if(auto result = validate_name(vector.name); !result)
                    return result;
                if(!Math::is_finite(vector.default_value))
                    return Result<void>::failure(
                        context.error("material", "vector default must be finite"));
            }
            return Result<void>::success();
        }

        void write_material(const ShaderProgramMaterial& material, Json::Writer& writer) {
            writer.key("material");
            writer.begin_object();
            writer.key("textures");
            writer.begin_array();
            for(const auto& texture : material.textures) {
                writer.begin_object();
                writer.field("name", texture.name);
                writer.field("display", texture.display_name);
                writer.field("optional", texture.optional);
                writer.end_object();
            }
            writer.end_array();
            writer.key("scalars");
            writer.begin_array();
            for(const auto& scalar : material.scalars) {
                writer.begin_object();
                writer.field("name", scalar.name);
                writer.field("display", scalar.display_name);
                writer.field("default", scalar.default_value);
                writer.field("min", scalar.min_value);
                writer.field("max", scalar.max_value);
                writer.field("step", scalar.step);
                writer.end_object();
            }
            writer.end_array();
            writer.key("vectors");
            writer.begin_array();
            for(const auto& vector : material.vectors) {
                writer.begin_object();
                writer.field("name", vector.name);
                writer.field("display", vector.display_name);
                writer.key("default");
                writer.begin_array();
                for(int component = 0; component < 4; ++component)
                    writer.value(vector.default_value[component]);
                writer.end_array();
                writer.field("color", vector.color);
                writer.end_object();
            }
            writer.end_array();
            writer.end_object();
        }

        Result<void> read_display(Json::Node item, std::string& display,
            const std::string& location, const Json::Context& context) {
            Json::Node value;
            if(item["display"].get(value))
                return Result<void>::success();
            auto parsed =
                context.read_scalar<std::string>(value, location + ".display", "a string");
            if(!parsed)
                return Result<void>::failure(parsed.error());
            display = std::move(parsed).value();
            return Result<void>::success();
        }

        Result<ShaderMaterialTexture> read_texture(Json::Node item, const Json::Context& context) {
            constexpr std::string_view location = "material.textures[]";
            if(auto valid = context.validate_keys(item, {"name", "display", "optional"}, location);
                !valid)
                return Result<ShaderMaterialTexture>::failure(valid.error());
            auto name = context.read_field<std::string>(item, "name", "a string", location);
            if(!name)
                return Result<ShaderMaterialTexture>::failure(name.error());
            ShaderMaterialTexture texture;
            texture.name = std::move(name).value();
            if(auto display =
                    read_display(item, texture.display_name, std::string(location), context);
                !display)
                return Result<ShaderMaterialTexture>::failure(display.error());
            Json::Node optional;
            if(!item["optional"].get(optional)) {
                auto value = context.read_scalar<bool>(
                    optional, "material.textures[].optional", "a boolean");
                if(!value)
                    return Result<ShaderMaterialTexture>::failure(value.error());
                texture.optional = value.value();
            }
            return Result<ShaderMaterialTexture>::success(std::move(texture));
        }

        Result<void> read_optional_float(Json::Node item, std::string_view field, float& target,
            std::string_view location, const Json::Context& context) {
            Json::Node value;
            if(item[field].get(value))
                return Result<void>::success();
            auto parsed = context.read_scalar<float>(
                value, std::string(location) + "." + std::string(field), "a finite number");
            if(!parsed)
                return Result<void>::failure(parsed.error());
            target = parsed.value();
            return Result<void>::success();
        }

        Result<ShaderMaterialScalar> read_scalar(Json::Node item, const Json::Context& context) {
            constexpr std::string_view location = "material.scalars[]";
            if(auto valid = context.validate_keys(
                   item, {"name", "display", "default", "min", "max", "step"}, location);
                !valid)
                return Result<ShaderMaterialScalar>::failure(valid.error());
            auto name = context.read_field<std::string>(item, "name", "a string", location);
            auto initial = context.read_field<float>(item, "default", "a finite number", location);
            if(!name)
                return Result<ShaderMaterialScalar>::failure(name.error());
            if(!initial)
                return Result<ShaderMaterialScalar>::failure(initial.error());
            ShaderMaterialScalar scalar;
            scalar.name = std::move(name).value();
            scalar.default_value = initial.value();
            if(auto display =
                    read_display(item, scalar.display_name, std::string(location), context);
                !display)
                return Result<ShaderMaterialScalar>::failure(display.error());
            for(const auto [field, target] :
                {std::pair<std::string_view, float*>{"min", &scalar.min_value},
                    {"max", &scalar.max_value}, {"step", &scalar.step}})
                if(auto valid = read_optional_float(item, field, *target, location, context);
                    !valid)
                    return Result<ShaderMaterialScalar>::failure(valid.error());
            return Result<ShaderMaterialScalar>::success(std::move(scalar));
        }

        Result<ShaderMaterialVector> read_vector(Json::Node item, const Json::Context& context) {
            constexpr std::string_view location = "material.vectors[]";
            if(auto valid =
                    context.validate_keys(item, {"name", "display", "default", "color"}, location);
                !valid)
                return Result<ShaderMaterialVector>::failure(valid.error());
            auto name = context.read_field<std::string>(item, "name", "a string", location);
            if(!name)
                return Result<ShaderMaterialVector>::failure(name.error());
            auto initial_node = context.required_child(item, "default", location);
            if(!initial_node)
                return Result<ShaderMaterialVector>::failure(initial_node.error());
            auto elements = context.array(initial_node.value(), "material.vectors[].default");
            if(!elements)
                return Result<ShaderMaterialVector>::failure(elements.error());
            if(elements.value().size() != 4)
                return Result<ShaderMaterialVector>::failure(
                    context.error("material.vectors[].default", "expected four numbers"));
            ShaderMaterialVector vector;
            vector.name = std::move(name).value();
            int index = 0;
            for(const auto element : elements.value()) {
                auto value = context.read_scalar<float>(element,
                    "material.vectors[].default[" + std::to_string(index) + "]", "a finite number");
                if(!value)
                    return Result<ShaderMaterialVector>::failure(value.error());
                vector.default_value[index++] = value.value();
            }
            if(auto display =
                    read_display(item, vector.display_name, std::string(location), context);
                !display)
                return Result<ShaderMaterialVector>::failure(display.error());
            Json::Node color;
            if(!item["color"].get(color)) {
                auto value =
                    context.read_scalar<bool>(color, "material.vectors[].color", "a boolean");
                if(!value)
                    return Result<ShaderMaterialVector>::failure(value.error());
                vector.color = value.value();
            }
            return Result<ShaderMaterialVector>::success(std::move(vector));
        }

        Result<ShaderProgramMaterial> read_material(Json::Node node, const Json::Context& context) {
            if(auto valid =
                    context.validate_keys(node, {"textures", "scalars", "vectors"}, "material");
                !valid)
                return Result<ShaderProgramMaterial>::failure(valid.error());
            ShaderProgramMaterial material;
            const auto read_array = [&](const std::string_view name, auto& destination,
                                        const auto& read_item) -> Result<void> {
                Json::Node array_node;
                if(node[name].get(array_node))
                    return Result<void>::success();
                const auto items =
                    context.array(array_node, std::string("material.") + std::string(name));
                if(!items)
                    return Result<void>::failure(items.error());
                for(const auto item : items.value()) {
                    auto value = read_item(item);
                    if(!value)
                        return Result<void>::failure(value.error());
                    destination.push_back(std::move(value).value());
                }
                return Result<void>::success();
            };
            if(auto result = read_array("textures", material.textures,
                   [&](Json::Node item) { return read_texture(item, context); });
                !result)
                return Result<ShaderProgramMaterial>::failure(result.error());
            if(auto result = read_array("scalars", material.scalars,
                   [&](Json::Node item) { return read_scalar(item, context); });
                !result)
                return Result<ShaderProgramMaterial>::failure(result.error());
            if(auto result = read_array("vectors", material.vectors,
                   [&](Json::Node item) { return read_vector(item, context); });
                !result)
                return Result<ShaderProgramMaterial>::failure(result.error());
            if(auto valid = validate_material(material, context); !valid)
                return Result<ShaderProgramMaterial>::failure(valid.error());
            return Result<ShaderProgramMaterial>::success(std::move(material));
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
            if(data.material) {
                if(auto valid = validate_material(*data.material, context); !valid)
                    return valid;
                write_material(*data.material, writer);
            }
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
            if(auto valid =
                    context.validate_keys(root, {"version", "vertex", "fragment", "material"});
                !valid)
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
            ShaderProgramData data{std::move(vertex).value(), std::move(fragment).value()};
            Json::Node material;
            if(!root["material"].get(material)) {
                auto parsed = read_material(material, context);
                if(!parsed)
                    return Result<ShaderProgramData>::failure(parsed.error());
                data.material = std::move(parsed).value();
            }
            return Result<ShaderProgramData>::success(std::move(data));
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

    Result<std::string> ShaderProgramSerializer::serialize_material(
        const ShaderProgramMaterial& material) const {
        const Json::Context context("shader material", "<memory>");
        if(auto valid = validate_material(material, context); !valid)
            return Result<std::string>::failure(valid.error());
        Json::Writer writer;
        writer.begin_object();
        write_material(material, writer);
        writer.end_object();
        return std::move(writer).finish();
    }

    Result<ShaderProgramMaterial> ShaderProgramSerializer::deserialize_material(
        const std::string_view contents) const {
        const Json::Context context("shader material", "<artifact>");
        simdjson::dom::parser parser;
        auto root = context.parse(parser, contents);
        if(!root)
            return Result<ShaderProgramMaterial>::failure(root.error());
        if(auto valid = context.validate_keys(root.value(), {"material"}); !valid)
            return Result<ShaderProgramMaterial>::failure(valid.error());
        auto material = context.required_child(root.value(), "material");
        if(!material)
            return Result<ShaderProgramMaterial>::failure(material.error());
        return read_material(material.value(), context);
    }
}
