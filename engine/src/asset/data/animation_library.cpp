#include "asset/data/animation_library.h"
#include "common/json.h"
#include "common/file_io.h"
#include <algorithm>
#include <unordered_set>

namespace Comet {
    namespace {
        Result<AssetHandle> read_handle(Json::Node node, const Json::Context& context) {
            auto text = context.read_field<std::string>(node, "mesh", "an asset handle string");
            if(!text)
                return Result<AssetHandle>::failure(text.error());
            const auto handle = AssetHandle::parse(text.value());
            if(!handle)
                return Result<AssetHandle>::failure("Invalid animation source handle");
            return Result<AssetHandle>::success(*handle);
        }
        Result<AnimationRetarget> read_retarget(Json::Node node, const Json::Context& context) {
            using Read = Result<AnimationRetarget>;
            if(auto keys = context.validate_keys(
                   node, {"mesh", "nodes", "translations", "translation_scale", "chains",
                             "source_pose", "target_pose"});
                !keys)
                return Read::failure(keys.error());
            AnimationRetarget result;
            auto scale =
                context.read_optional_field<float>(node, "translation_scale", "a finite number");
            if(!scale)
                return Read::failure(scale.error());
            if(scale.value())
                result.translation_scale = *scale.value();
            auto aliases = context.find(node, "nodes");
            if(!aliases)
                return Read::failure(aliases.error());
            if(aliases.value()) {
                auto values = context.object(*aliases.value(), "nodes");
                if(!values)
                    return Read::failure(values.error());
                for(const auto field : values.value()) {
                    auto name = context.read_scalar<std::string>(
                        field.value, field.key, "a target node name");
                    if(!name)
                        return Read::failure(name.error());
                    result.nodes.emplace(std::string(field.key), std::move(name).value());
                }
            }
            auto translations = context.find(node, "translations");
            if(!translations)
                return Read::failure(translations.error());
            if(translations.value()) {
                auto values = context.array(*translations.value(), "translations");
                if(!values)
                    return Read::failure(values.error());
                for(const auto value : values.value()) {
                    auto name = context.read_scalar<std::string>(
                        value, "translations", "a source node name");
                    if(!name)
                        return Read::failure(name.error());
                    result.translations.push_back(std::move(name).value());
                }
            }
            for(const auto& [key, target] : {std::pair{"source_pose", &result.source_pose},
                    {"target_pose", &result.target_pose}}) {
                auto value = context.find(node, key);
                if(!value)
                    return Read::failure(value.error());
                if(!value.value())
                    continue;
                auto object = context.object(*value.value(), key);
                if(!object)
                    return Read::failure(object.error());
                for(const auto field : object.value()) {
                    auto vector = Json::read_vector<Math::Vec3>(field.value, context, field.key);
                    if(!vector)
                        return Read::failure(vector.error());
                    if(!target
                            ->emplace(std::string(field.key),
                                Math::Vec3(vector.value()[0], vector.value()[1], vector.value()[2]))
                            .second)
                        return Read::failure("Duplicate reference pose bone");
                }
            }
            auto chains = context.find(node, "chains");
            if(!chains)
                return Read::failure(chains.error());
            if(chains.value()) {
                auto values = context.array(*chains.value(), "chains");
                if(!values)
                    return Read::failure(values.error());
                for(auto value : values.value()) {
                    if(auto keys = context.validate_keys(value,
                           {"source_root", "source_tip", "target_root", "target_tip", "weight"});
                        !keys)
                        return Read::failure(keys.error());
                    AnimationRetarget::Chain chain;
                    for(const auto& [name, field] : {std::pair{"source_root", &chain.source_root},
                            std::pair{"source_tip", &chain.source_tip},
                            std::pair{"target_root", &chain.target_root},
                            std::pair{"target_tip", &chain.target_tip}}) {
                        auto text = context.read_field<std::string>(value, name, "a bone name");
                        if(!text)
                            return Read::failure(text.error());
                        *field = std::move(text).value();
                    }
                    auto weight =
                        context.read_optional_field<float>(value, "weight", "a finite weight");
                    if(!weight)
                        return Read::failure(weight.error());
                    if(weight.value())
                        chain.weight = *weight.value();
                    result.chains.push_back(std::move(chain));
                }
            }
            return Read::success(std::move(result));
        }
    }
    Result<void> AnimationLibrary::validate() const {
        if(entries.empty() || entries.size() > 256)
            return Result<void>::failure("Animation library needs 1..256 entries");
        std::unordered_set<std::string> names;
        for(const auto& entry : entries)
            if(entry.name.empty() || entry.clip.empty() || !entry.mesh
                || !names.insert(entry.name).second)
                return Result<void>::failure(
                    "Invalid or duplicate animation library entry: " + entry.name);
        for(const auto& [source, settings] : retargets) {
            if(std::ranges::none_of(
                   entries, [&](const Entry& entry) { return entry.mesh == source; }))
                return Result<void>::failure("Retarget source is not used by this library");
            if(auto valid = settings.validate(); !valid)
                return valid;
        }
        return Result<void>::success();
    }
    Result<AnimationData> AnimationLibrary::bind(const AnimationData& target) const {
        using Bound = Result<AnimationData>;
        if(auto valid = validate(); !valid)
            return Bound::failure(valid.error());
        if(auto valid = validate_animation(target); !valid)
            return Bound::failure(valid.error());
        // 节点名称是跨导入身份；重复名称不猜测，避免静默错绑。
        std::map<std::string, uint32_t> nodes;
        for(uint32_t i = 0; i < target.nodes.size(); ++i)
            if(target.nodes[i].name.empty() || !nodes.emplace(target.nodes[i].name, i).second)
                return Bound::failure("Animation library target needs unique node names");
        AnimationData result{.mesh_node = target.mesh_node,
            .nodes = target.nodes,
            .evaluation_order = target.evaluation_order,
            .joints = target.joints};
        result.clips.reserve(entries.size());
        std::map<AssetHandle, std::vector<uint32_t>> mappings;
        std::map<AssetHandle, AnimationData> converted;
        for(const auto& entry : entries) {
            const auto found = sources.find(entry.mesh);
            if(found == sources.end() || !found->second)
                return Bound::failure("Animation library source is not loaded: " + entry.name);
            const auto& source = *found->second;
            if(const auto settings = retargets.find(entry.mesh); settings != retargets.end()) {
                auto prepared = converted.find(entry.mesh);
                if(prepared == converted.end()) {
                    auto bound = retarget_animation(source, target, settings->second);
                    if(!bound)
                        return Bound::failure(bound.error());
                    prepared = converted.emplace(entry.mesh, std::move(bound).value()).first;
                }
                const auto* clip = prepared->second.find_clip(entry.clip);
                if(!clip)
                    return Bound::failure("Animation library clip is missing: " + entry.clip);
                auto copy = *clip;
                copy.name = entry.name;
                result.clips.push_back(std::move(copy));
                continue;
            }
            auto [mapping, inserted] = mappings.try_emplace(entry.mesh);
            if(inserted) {
                if(auto valid = validate_animation(source); !valid)
                    return Bound::failure(valid.error());
                std::unordered_set<std::string> names;
                for(const auto& node : source.nodes) {
                    const auto match = nodes.find(node.name);
                    if(match == nodes.end() || !names.insert(node.name).second)
                        return Bound::failure("Missing or ambiguous animation node: " + node.name);
                    mapping->second.push_back(match->second);
                }
                if(mapping->second[source.mesh_node] != target.mesh_node)
                    return Bound::failure("Animation library mesh space does not match target");
                for(std::size_t i = 0; i < source.nodes.size(); ++i) {
                    const auto& node = source.nodes[i];
                    const auto& other = target.nodes[mapping->second[i]];
                    const auto parent = node.parent == NO_ANIMATION_NODE
                                            ? NO_ANIMATION_NODE
                                            : mapping->second[node.parent];
                    const bool same_rest =
                        Math::length(node.rest.translation - other.rest.translation) < 0.0001f
                        && Math::length(node.rest.scale - other.rest.scale) < 0.0001f
                        && std::abs(Math::dot(Math::normalize(node.rest.rotation),
                               Math::normalize(other.rest.rotation)))
                               > .99999f;
                    if(parent != other.parent || !same_rest)
                        return Bound::failure("Incompatible animation skeleton at: " + node.name);
                }
            }
            const auto* clip = source.find_clip(entry.clip);
            if(!clip)
                return Bound::failure("Animation library clip is missing: " + entry.clip);
            auto copy = *clip;
            copy.name = entry.name;
            for(auto& channel : copy.channels)
                channel.node = mapping->second[channel.node];
            result.clips.push_back(std::move(copy));
        }
        return Bound::success(std::move(result));
    }
    Result<AnimationLibrary> AnimationLibrary::deserialize(
        std::string_view contents, std::string_view source) {
        return Json::deserialize<AnimationLibrary>("animation library", contents, source,
            [](Json::Node root, const Json::Context& context) -> Result<AnimationLibrary> {
                using Read = Result<AnimationLibrary>;
                if(auto keys = context.validate_keys(root, {"clips", "retargets"}); !keys)
                    return Read::failure(keys.error());
                auto child = context.required_child(root, "clips");
                if(!child)
                    return Read::failure(child.error());
                auto clips = context.array(child.value(), "clips");
                if(!clips)
                    return Read::failure(clips.error());
                AnimationLibrary result;
                for(const auto node : clips.value()) {
                    if(auto keys = context.validate_keys(node, {"name", "mesh", "clip"}); !keys)
                        return Read::failure(keys.error());
                    auto name = context.read_field<std::string>(node, "name", "a name");
                    auto mesh = read_handle(node, context);
                    auto clip = context.read_field<std::string>(node, "clip", "a clip name");
                    if(!name)
                        return Read::failure(name.error());
                    if(!mesh)
                        return Read::failure(mesh.error());
                    if(!clip)
                        return Read::failure(clip.error());
                    result.entries.push_back(
                        {std::move(name).value(), mesh.value(), std::move(clip).value()});
                }
                auto profiles = context.find(root, "retargets");
                if(!profiles)
                    return Read::failure(profiles.error());
                if(profiles.value()) {
                    auto values = context.array(*profiles.value(), "retargets");
                    if(!values)
                        return Read::failure(values.error());
                    for(const auto node : values.value()) {
                        auto source = read_handle(node, context);
                        auto settings = read_retarget(node, context);
                        if(!source)
                            return Read::failure(source.error());
                        if(!settings)
                            return Read::failure(settings.error());
                        if(!result.retargets.emplace(source.value(), std::move(settings).value())
                                .second)
                            return Read::failure("Duplicate retarget source");
                    }
                }
                if(auto valid = result.validate(); !valid)
                    return Read::failure(valid.error());
                return Read::success(std::move(result));
            });
    }
    Result<AnimationLibrary> AnimationLibrary::load(const std::filesystem::path& path) {
        return Json::load(AnimationLibrary{}, path);
    }
    Result<std::string> AnimationLibrary::serialize() const {
        if(auto valid = validate(); !valid)
            return Result<std::string>::failure(valid.error());
        Json::Writer writer;
        writer.begin_object();
        writer.key("clips");
        writer.begin_array();
        for(const auto& entry : entries) {
            writer.begin_object();
            writer.field("name", entry.name);
            writer.field("mesh", std::to_string(entry.mesh.value()));
            writer.field("clip", entry.clip);
            writer.end_object();
        }
        writer.end_array();
        if(!retargets.empty()) {
            writer.key("retargets");
            writer.begin_array();
            for(const auto& [source, settings] : retargets) {
                writer.begin_object();
                writer.field("mesh", std::to_string(source.value()));
                writer.field("translation_scale", settings.translation_scale);
                for(const auto& [name, pose] : {std::pair{"source_pose", &settings.source_pose},
                        {"target_pose", &settings.target_pose}}) {
                    writer.key(name);
                    writer.begin_object();
                    for(const auto& [bone, value] : *pose) {
                        writer.key(bone);
                        writer.begin_array();
                        writer.value(value.x);
                        writer.value(value.y);
                        writer.value(value.z);
                        writer.end_array();
                    }
                    writer.end_object();
                }
                writer.key("nodes");
                writer.begin_object();
                for(const auto& [from, to] : settings.nodes)
                    writer.field(from, to);
                writer.end_object();
                writer.key("translations");
                writer.begin_array();
                for(const auto& name : settings.translations)
                    writer.value(name);
                writer.end_array();
                if(!settings.chains.empty()) {
                    writer.key("chains");
                    writer.begin_array();
                    for(const auto& chain : settings.chains) {
                        writer.begin_object();
                        writer.field("source_root", chain.source_root);
                        writer.field("source_tip", chain.source_tip);
                        writer.field("target_root", chain.target_root);
                        writer.field("target_tip", chain.target_tip);
                        writer.field("weight", chain.weight);
                        writer.end_object();
                    }
                    writer.end_array();
                }
                writer.end_object();
            }
            writer.end_array();
        }
        writer.end_object();
        return std::move(writer).finish();
    }
    Result<void> AnimationLibrary::save(const std::filesystem::path& path) const {
        auto text = serialize();
        if(!text)
            return Result<void>::failure(text.error());
        return write_text_file_atomic(path, text.value());
    }
}
