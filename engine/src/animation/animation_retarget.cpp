#include "animation/animation_retarget.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <unordered_set>

namespace Comet {
    namespace {
        struct RestFrame {
            Math::Vec3 position;
            Math::Quat rotation;
            float scale;
        };
        Result<std::vector<RestFrame>> rest_frames(const AnimationData& data) {
            std::vector<Math::Mat4> world(data.nodes.size());
            for(const auto i : data.evaluation_order) {
                const auto& rest = data.nodes[i].rest;
                world[i] = Math::translate(Math::Mat4(1), rest.translation)
                           * glm::mat4_cast(rest.rotation) * Math::scale(Math::Mat4(1), rest.scale);
                if(data.nodes[i].parent != NO_ANIMATION_NODE)
                    world[i] = world[data.nodes[i].parent] * world[i];
            }
            const auto basis = glm::affineInverse(world[data.mesh_node]);
            std::vector<RestFrame> frames;
            frames.reserve(world.size() + 1);
            for(const auto& matrix : world) {
                const auto local = basis * matrix;
                const auto scale = Math::uniform_scale(local);
                if(!scale)
                    return Result<std::vector<RestFrame>>::failure(
                        "Retargeting requires positive uniform rest scales");
                frames.push_back({Math::Vec3(local[3]),
                    glm::normalize(glm::quat_cast(Math::Mat3(local) / *scale)), *scale});
            }
            const auto scale = Math::uniform_scale(basis);
            if(!scale)
                return Result<std::vector<RestFrame>>::failure("Invalid retarget mesh basis");
            // 最后一个元素表示根节点的父空间，仍使用对应网格的坐标系。
            frames.push_back({Math::Vec3(basis[3]),
                glm::normalize(glm::quat_cast(Math::Mat3(basis) / *scale)), *scale});
            return Result<std::vector<RestFrame>>::success(std::move(frames));
        }
        Result<std::map<std::string, uint32_t>> named_nodes(const AnimationData& data) {
            std::map<std::string, uint32_t> nodes;
            for(uint32_t i = 0; i < data.nodes.size(); ++i)
                if(data.nodes[i].name.empty() || !nodes.emplace(data.nodes[i].name, i).second)
                    return Result<std::map<std::string, uint32_t>>::failure(
                        "Retargeting needs unique node names");
            return Result<std::map<std::string, uint32_t>>::success(std::move(nodes));
        }
        bool channel_moves(const AnimationChannel& channel, const JointTransform& rest) {
            const bool cubic =
                channel.interpolation == AnimationChannel::Interpolation::CubicSpline;
            for(std::size_t i = 0; i < channel.values.size(); ++i) {
                const auto& value = channel.values[i];
                if(cubic && i % 3 != 1) {
                    if(glm::length(value) > .0001f)
                        return true;
                    continue;
                }
                if(channel.path == AnimationChannel::Path::Rotation) {
                    const auto rotation =
                        glm::normalize(Math::Quat(value.w, value.x, value.y, value.z));
                    if(std::abs(glm::dot(rotation, rest.rotation)) < .99999f)
                        return true;
                } else {
                    const auto expected = channel.path == AnimationChannel::Path::Scale
                                              ? rest.scale
                                              : rest.translation;
                    if(glm::length(Math::Vec3(value) - expected) > .0001f)
                        return true;
                }
            }
            return false;
        }
    }
    Result<void> AnimationRetarget::validate() const {
        if(nodes.size() > 4096 || translations.size() > 128 || !std::isfinite(translation_scale)
            || translation_scale < 0)
            return Result<void>::failure("Invalid retarget settings");
        std::unordered_set<std::string> targets;
        for(const auto& [source, target] : nodes)
            if(source.empty() || target.empty() || !targets.insert(target).second)
                return Result<void>::failure("Invalid or duplicate retarget node: " + source);
        std::unordered_set<std::string> names;
        for(const auto& name : translations)
            if(name.empty() || !names.insert(name).second)
                return Result<void>::failure("Invalid or duplicate translation node: " + name);
        return Result<void>::success();
    }
    Result<AnimationData> retarget_animation(const AnimationData& source,
        const AnimationData& target, const AnimationRetarget& settings) {
        using Bound = Result<AnimationData>;
        for(auto valid :
            {settings.validate(), validate_animation(source), validate_animation(target)})
            if(!valid)
                return Bound::failure(valid.error());
        auto source_names = named_nodes(source), target_names = named_nodes(target);
        if(!source_names)
            return Bound::failure(source_names.error());
        if(!target_names)
            return Bound::failure(target_names.error());
        for(const auto& [from, to] : settings.nodes)
            if(!source_names.value().contains(from) || !target_names.value().contains(to))
                return Bound::failure("Retarget mapping node is missing: " + from + " -> " + to);
        std::vector<uint32_t> mapping(source.nodes.size(), NO_ANIMATION_NODE);
        std::vector<uint32_t> reverse(target.nodes.size(), NO_ANIMATION_NODE);
        for(uint32_t i = 0; i < source.nodes.size(); ++i) {
            const auto alias = settings.nodes.find(source.nodes[i].name);
            const auto& name = alias != settings.nodes.end() ? alias->second : source.nodes[i].name;
            const auto found = target_names.value().find(name);
            uint32_t destination =
                found != target_names.value().end() ? found->second : NO_ANIMATION_NODE;
            if(i == source.mesh_node) {
                if(alias != settings.nodes.end() && destination != target.mesh_node)
                    return Bound::failure("Retarget mesh node must map to the target mesh node");
                destination = target.mesh_node;
            }
            if(destination == NO_ANIMATION_NODE)
                continue;
            if(reverse[destination] != NO_ANIMATION_NODE)
                return Bound::failure(
                    "Multiple source nodes map to: " + target.nodes[destination].name);
            mapping[i] = destination;
            reverse[destination] = i;
        }
        const bool mapped_joint = std::ranges::any_of(source.joints, [&](const SkinJoint& joint) {
            return mapping[joint.node] != NO_ANIMATION_NODE
                   && std::ranges::any_of(target.joints,
                       [&](const SkinJoint& other) { return other.node == mapping[joint.node]; });
        });
        if(!mapped_joint)
            return Bound::failure("Retarget mapping contains no corresponding skin joints");
        const auto mapped_parent = [](const AnimationData& data, uint32_t node,
                                       const std::vector<uint32_t>& map) {
            auto parent = data.nodes[node].parent;
            while(parent != NO_ANIMATION_NODE && map[parent] == NO_ANIMATION_NODE)
                parent = data.nodes[parent].parent;
            return parent;
        };
        for(uint32_t i = 0; i < source.nodes.size(); ++i) {
            if(mapping[i] == NO_ANIMATION_NODE)
                continue;
            const auto from = mapped_parent(source, i, mapping);
            const auto to = mapped_parent(target, mapping[i], reverse);
            if((from == NO_ANIMATION_NODE) != (to == NO_ANIMATION_NODE)
                || (from != NO_ANIMATION_NODE && mapping[from] != to))
                return Bound::failure(
                    "Retarget hierarchy does not match at: " + source.nodes[i].name);
        }
        std::unordered_set<uint32_t> translated;
        for(const auto& name : settings.translations) {
            const auto found = source_names.value().find(name);
            if(found == source_names.value().end() || mapping[found->second] == NO_ANIMATION_NODE)
                return Bound::failure("Translation node is not mapped: " + name);
            translated.insert(found->second);
        }
        auto source_rest = rest_frames(source), target_rest = rest_frames(target);
        if(!source_rest)
            return Bound::failure(source_rest.error());
        if(!target_rest)
            return Bound::failure(target_rest.error());
        float scale = settings.translation_scale;
        if(scale == 0) {
            std::vector<float> ratios;
            for(uint32_t i = 0; i < source.nodes.size(); ++i) {
                if(mapping[i] == NO_ANIMATION_NODE)
                    continue;
                const auto parent = mapped_parent(source, i, mapping);
                if(parent == NO_ANIMATION_NODE)
                    continue;
                const auto source_length = glm::length(
                    source_rest.value()[i].position - source_rest.value()[parent].position);
                const auto target_length =
                    glm::length(target_rest.value()[mapping[i]].position
                                - target_rest.value()[mapping[parent]].position);
                if(source_length > 1e-5f && target_length > 1e-5f)
                    ratios.push_back(target_length / source_length);
            }
            if(ratios.empty()) {
                if(!translated.empty())
                    return Bound::failure(
                        "Cannot infer translation scale; set an explicit positive scale");
                scale = 1;
            } else {
                std::ranges::sort(ratios);
                scale = ratios[ratios.size() / 2];
                if(ratios.size() % 2 == 0)
                    scale = (scale + ratios[ratios.size() / 2 - 1]) * .5f;
            }
        }
        AnimationData output{.mesh_node = target.mesh_node,
            .nodes = target.nodes,
            .evaluation_order = target.evaluation_order,
            .joints = target.joints};
        for(const auto& clip : source.clips) {
            AnimationClip converted{.name = clip.name, .duration = clip.duration};
            for(const auto& channel : clip.channels) {
                const auto from = channel.node, to = mapping[from];
                auto mesh_ancestor = source.mesh_node;
                while(mesh_ancestor != NO_ANIMATION_NODE && mesh_ancestor != from)
                    mesh_ancestor = source.nodes[mesh_ancestor].parent;
                if(mesh_ancestor == from && channel_moves(channel, source.nodes[from].rest))
                    return Bound::failure(
                        "Retargeting requires a static mesh basis: " + source.nodes[from].name);
                if(to == NO_ANIMATION_NODE) {
                    if(channel_moves(channel, source.nodes[from].rest))
                        for(uint32_t i = 0; i < source.nodes.size(); ++i) {
                            if(mapping[i] == NO_ANIMATION_NODE)
                                continue;
                            auto ancestor = source.nodes[i].parent;
                            while(ancestor != NO_ANIMATION_NODE && ancestor != from)
                                ancestor = source.nodes[ancestor].parent;
                            if(ancestor == from)
                                return Bound::failure("Animated ancestor needs a mapping: "
                                                      + source.nodes[from].name);
                        }
                    continue;
                }
                if(channel.path == AnimationChannel::Path::Scale) {
                    if(channel_moves(channel, source.nodes[from].rest))
                        return Bound::failure("Animated scale retargeting is unsupported: "
                                              + source.nodes[from].name);
                    continue;
                }
                if(channel.path == AnimationChannel::Path::Translation
                    && !translated.contains(from))
                    continue;
                const auto source_parent = source.nodes[from].parent;
                const auto target_parent = target.nodes[to].parent;
                const auto& first =
                    source_rest.value()[source_parent == NO_ANIMATION_NODE ? source.nodes.size()
                                                                           : source_parent];
                const auto& second =
                    target_rest.value()[target_parent == NO_ANIMATION_NODE ? target.nodes.size()
                                                                           : target_parent];
                const auto pre = glm::inverse(second.rotation) * first.rotation;
                const auto post = glm::inverse(source_rest.value()[from].rotation)
                                  * target_rest.value()[to].rotation;
                auto copy = channel;
                copy.node = to;
                const bool cubic =
                    channel.interpolation == AnimationChannel::Interpolation::CubicSpline;
                for(std::size_t i = 0; i < copy.values.size(); ++i) {
                    auto& value = copy.values[i];
                    if(channel.path == AnimationChannel::Path::Rotation) {
                        auto rotation = pre * Math::Quat(value.w, value.x, value.y, value.z) * post;
                        if(!cubic || i % 3 == 1)
                            rotation = glm::normalize(rotation);
                        value = {rotation.x, rotation.y, rotation.z, rotation.w};
                    } else {
                        auto translation = Math::Vec3(value);
                        if(!cubic || i % 3 == 1)
                            translation -= source.nodes[from].rest.translation;
                        translation = pre * translation * (scale * first.scale / second.scale);
                        if(!cubic || i % 3 == 1)
                            translation += target.nodes[to].rest.translation;
                        value = Math::Vec4(translation, 0);
                    }
                }
                converted.channels.push_back(std::move(copy));
            }
            output.clips.push_back(std::move(converted));
        }
        if(auto valid = validate_animation(output); !valid)
            return Bound::failure(valid.error());
        return Bound::success(std::move(output));
    }
}
