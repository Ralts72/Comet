#include "asset/data/animation_library.h"
#include "common/json.h"
#include "common/file_io.h"
#include <algorithm>
#include <charconv>
#include <unordered_set>

namespace Comet {
    Result<void> AnimationLibrary::validate() const {
        if(entries.empty() || entries.size() > 256)
            return Result<void>::failure("Animation library needs 1..256 entries");
        std::unordered_set<std::string> names;
        for(const auto& entry : entries)
            if(entry.name.empty() || entry.clip.empty() || !entry.mesh
                || !names.insert(entry.name).second)
                return Result<void>::failure(
                    "Invalid or duplicate animation library entry: " + entry.name);
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
        for(const auto& entry : entries) {
            const auto found = sources.find(entry.mesh);
            if(found == sources.end() || !found->second)
                return Bound::failure("Animation library source is not loaded: " + entry.name);
            const auto& source = *found->second;
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
                        glm::length(node.rest.translation - other.rest.translation) < 0.0001f
                        && glm::length(node.rest.scale - other.rest.scale) < 0.0001f
                        && std::abs(glm::dot(glm::normalize(node.rest.rotation),
                               glm::normalize(other.rest.rotation)))
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
                if(auto keys = context.validate_keys(root, {"clips"}); !keys)
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
                    auto mesh =
                        context.read_field<std::string>(node, "mesh", "an asset handle string");
                    auto clip = context.read_field<std::string>(node, "clip", "a clip name");
                    if(!name)
                        return Read::failure(name.error());
                    if(!mesh)
                        return Read::failure(mesh.error());
                    if(!clip)
                        return Read::failure(clip.error());
                    uint64_t handle = 0;
                    const auto parsed = std::from_chars(
                        mesh.value().data(), mesh.value().data() + mesh.value().size(), handle);
                    if(parsed.ec != std::errc{}
                        || parsed.ptr != mesh.value().data() + mesh.value().size() || !handle)
                        return Read::failure("Invalid animation source handle");
                    result.entries.push_back(
                        {std::move(name).value(), AssetHandle(handle), std::move(clip).value()});
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
