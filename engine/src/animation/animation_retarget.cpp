#include "animation/animation_retarget.h"
#include <cctype>
#include <set>
#include <algorithm>
#include <unordered_set>

namespace Comet {
    namespace {
        struct RestFrame {
            Math::Vec3 position;
            Math::Quat rotation;
            float scale;
        };
        Result<std::vector<RestFrame>> rest_frames(
            const AnimationData& data, std::span<const JointTransform> local = {}) {
            std::vector<Math::Mat4> world(data.nodes.size());
            for(const auto i : data.evaluation_order) {
                const auto& rest = local.empty() ? data.nodes[i].rest : local[i];
                world[i] = Math::translate(Math::Mat4(1), rest.translation)
                           * Math::to_mat4(rest.rotation) * Math::scale(Math::Mat4(1), rest.scale);
                if(data.nodes[i].parent != NO_ANIMATION_NODE)
                    world[i] = world[data.nodes[i].parent] * world[i];
            }
            const auto basis = Math::affine_inverse(world[data.mesh_node]);
            std::vector<RestFrame> frames;
            frames.reserve(world.size() + 1);
            for(const auto& matrix : world) {
                const auto local = basis * matrix;
                const auto scale = Math::uniform_scale(local);
                if(!scale)
                    return Result<std::vector<RestFrame>>::failure(
                        "Retargeting requires positive uniform rest scales");
                frames.push_back({Math::Vec3(local[3]),
                    Math::normalize(Math::to_quat(Math::Mat3(local) / *scale)), *scale});
            }
            const auto scale = Math::uniform_scale(basis);
            if(!scale)
                return Result<std::vector<RestFrame>>::failure("Invalid retarget mesh basis");
            // 最后一个元素表示根节点的父空间，仍使用对应网格的坐标系。
            frames.push_back({Math::Vec3(basis[3]),
                Math::normalize(Math::to_quat(Math::Mat3(basis) / *scale)), *scale});
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
        Result<std::vector<JointTransform>> reference_transforms(const AnimationData& data,
            const std::map<std::string, uint32_t>& names,
            const std::map<std::string, Math::Vec3>& corrections) {
            using Posed = Result<std::vector<JointTransform>>;
            if(corrections.size() > 256)
                return Posed::failure("Too many reference pose corrections");
            std::vector<JointTransform> local;
            local.reserve(data.nodes.size());
            for(const auto& node : data.nodes)
                local.push_back(node.rest);
            for(const auto& [name, degrees] : corrections) {
                const auto found = names.find(name);
                if(found == names.end())
                    return Posed::failure("Reference pose bone is missing: " + name);
                if(!Math::is_finite(degrees)
                    || Math::any_greater(Math::abs(degrees), Math::Vec3(180)))
                    return Posed::failure("Invalid reference pose correction: " + name);
                auto ancestor = data.mesh_node;
                while(ancestor != NO_ANIMATION_NODE && ancestor != found->second)
                    ancestor = data.nodes[ancestor].parent;
                if(ancestor == found->second)
                    return Posed::failure("Reference pose cannot rotate the mesh basis");
                auto& rotation = local[found->second].rotation;
                rotation = Math::normalize(rotation * Math::Quat(Math::radians(degrees)));
            }
            return Posed::success(std::move(local));
        }
        Result<std::vector<RestFrame>> calibrated_rest_frames(const AnimationData& data,
            const std::map<std::string, uint32_t>& names,
            const std::map<std::string, Math::Vec3>& corrections) {
            if(corrections.empty())
                return rest_frames(data);
            auto local = reference_transforms(data, names, corrections);
            if(!local)
                return Result<std::vector<RestFrame>>::failure(local.error());
            return rest_frames(data, local.value());
        }
        std::string bone_semantic(std::string_view name) {
            if(const auto split = name.find_last_of(":|/"); split != std::string_view::npos)
                name.remove_prefix(split + 1);
            std::string key;
            for(unsigned char character : name)
                if(std::isalnum(character))
                    key.push_back(char(std::tolower(character)));
            for(std::string_view prefix : {"mixamorig", "armature", "bip001", "source", "src"})
                if(key.starts_with(prefix))
                    key.erase(0, prefix.size());
            std::string side;
            if(key.starts_with("left")) {
                side = "l";
                key.erase(0, 4);
            } else if(key.starts_with("right")) {
                side = "r";
                key.erase(0, 5);
            } else if(key.ends_with("left")) {
                side = "l";
                key.resize(key.size() - 4);
            } else if(key.ends_with("right")) {
                side = "r";
                key.resize(key.size() - 5);
            }
            const auto canonical = [](std::string value) {
                constexpr std::pair<std::string_view, std::string_view> aliases[]{
                    {"hips", "pelvis"}, {"hip", "pelvis"}, {"body", "pelvis"}, {"arm", "upperarm"},
                    {"forearm", "lowerarm"}, {"wrist", "hand"}, {"upleg", "thigh"},
                    {"upperleg", "thigh"}, {"leg", "calf"}, {"lowerleg", "calf"}, {"ankle", "foot"},
                    {"shoulder", "clavicle"}, {"toebase", "toe"}, {"toes", "toe"},
                    {"torso", "spine"}, {"spine001", "spine1"}, {"spine002", "spine2"}};
                for(const auto& [from, to] : aliases)
                    if(value == from)
                        return std::string(to);
                return value;
            };
            if(side.empty() && key.size() > 1 && (key.back() == 'l' || key.back() == 'r')) {
                auto base = canonical(key.substr(0, key.size() - 1));
                if(base == "upperarm" || base == "lowerarm" || base == "hand" || base == "thigh"
                    || base == "calf" || base == "foot" || base == "clavicle" || base == "toe") {
                    side = key.back();
                    key = std::move(base);
                }
            }
            return canonical(std::move(key)) + side;
        }
        struct RetargetChain {
            std::vector<uint32_t> source, target;
            std::vector<float> source_distance, target_distance;
            float weight;
        };
        Result<std::vector<uint32_t>> chain_nodes(const AnimationData& data,
            const std::map<std::string, uint32_t>& names, const std::string& root,
            const std::string& tip) {
            using Read = Result<std::vector<uint32_t>>;
            if(!names.contains(root) || !names.contains(tip))
                return Read::failure("Retarget chain endpoint is missing: " + root + " -> " + tip);
            std::vector<uint32_t> path;
            auto node = names.at(tip);
            while(node != NO_ANIMATION_NODE) {
                path.push_back(node);
                if(node == names.at(root))
                    break;
                node = data.nodes[node].parent;
            }
            if(node == NO_ANIMATION_NODE || path.size() < 2
                || std::ranges::find(path, data.mesh_node) != path.end())
                return Read::failure("Retarget chain needs distinct ancestor/descendant bones");
            std::ranges::reverse(path);
            return Read::success(std::move(path));
        }
        Result<std::vector<float>> chain_distances(
            const std::vector<uint32_t>& nodes, const std::vector<RestFrame>& rest) {
            std::vector<float> distance(nodes.size(), 0);
            for(size_t i = 1; i < nodes.size(); ++i)
                distance[i] = distance[i - 1]
                              + Math::length(rest[nodes[i]].position - rest[nodes[i - 1]].position);
            if(distance.back() < 1e-5f)
                return Result<std::vector<float>>::failure("Retarget chain has zero rest length");
            const auto length = distance.back();
            for(auto& value : distance)
                value /= length;
            return Result<std::vector<float>>::success(std::move(distance));
        }
        std::vector<Math::Quat> mesh_rotations(
            const AnimationData& data, const std::vector<JointTransform>& local) {
            std::vector<Math::Quat> rotations(local.size());
            for(auto node : data.evaluation_order) {
                const auto parent = data.nodes[node].parent;
                rotations[node] = local[node].rotation;
                if(parent != NO_ANIMATION_NODE)
                    rotations[node] = rotations[parent] * rotations[node];
            }
            const auto basis = Math::inverse(rotations[data.mesh_node]);
            for(auto& value : rotations)
                value = Math::normalize(basis * value);
            return rotations;
        }
        Result<void> bake_chains(const AnimationData& source, AnimationData& target,
            const std::vector<RetargetChain>& chains, const std::vector<RestFrame>& source_rest,
            const std::vector<RestFrame>& target_rest) {
            if(chains.empty())
                return Result<void>::success();
            struct RotationSample {
                uint32_t node, first, last;
                float alpha, weight;
            };
            std::vector<RotationSample> samples;
            std::vector<int> channel_for_node(target.nodes.size(), -1);
            for(const auto& chain : chains)
                for(size_t i = 0; i < chain.target.size(); ++i) {
                    const float distance = chain.target_distance[i];
                    const auto upper = std::upper_bound(
                        chain.source_distance.begin(), chain.source_distance.end(), distance);
                    const size_t last = std::min(
                        size_t(upper - chain.source_distance.begin()), chain.source.size() - 1);
                    const size_t first = last ? last - 1 : 0;
                    const float span = chain.source_distance[last] - chain.source_distance[first];
                    channel_for_node[chain.target[i]] = int(samples.size());
                    samples.push_back({chain.target[i], chain.source[first], chain.source[last],
                        span > 0 ? (distance - chain.source_distance[first]) / span : 1,
                        chain.weight});
                }
            std::vector<Math::Quat> inverse_rest;
            inverse_rest.reserve(source_rest.size());
            for(const auto& frame : source_rest)
                inverse_rest.push_back(Math::inverse(frame.rotation));
            for(size_t clip_index = 0; clip_index < source.clips.size(); ++clip_index) {
                const auto& clip = source.clips[clip_index];
                if(clip.duration > 600)
                    return Result<void>::failure("Chain retarget clips are limited to 600 seconds");
                std::vector<float> times{0, clip.duration};
                for(uint32_t frame = 1; frame < uint32_t(std::ceil(clip.duration * 60)); ++frame)
                    times.push_back(std::min(frame / 60.f, clip.duration));
                for(const auto& channel : clip.channels)
                    for(auto time : channel.times) {
                        times.push_back(time);
                        if(channel.interpolation == AnimationChannel::Interpolation::Step
                            && time > 0)
                            times.push_back(std::nextafter(time, 0.f));
                    }
                std::ranges::sort(times);
                times.erase(std::unique(times.begin(), times.end()), times.end());
                if(times.size() > 65536)
                    return Result<void>::failure("Chain retarget sample budget exceeded");
                if(times.size() * samples.size() > 4 * 1024 * 1024)
                    return Result<void>::failure("Chain retarget key budget exceeded");
                std::vector<AnimationChannel> baked;
                baked.reserve(samples.size());
                for(const auto& sample : samples) {
                    baked.push_back({sample.node, AnimationChannel::Path::Rotation,
                        AnimationChannel::Interpolation::Linear, times, {}});
                    baked.back().values.reserve(times.size());
                }
                for(auto time : times) {
                    AnimationPlayback frame{
                        .current = {.clip = clip.name, .time = time, .loop = false}};
                    auto first = sample_animation(source, &frame, false);
                    auto second = sample_animation(target, &frame, false);
                    if(!first || !second)
                        return Result<void>::failure(!first ? first.error() : second.error());
                    const auto from = mesh_rotations(source, first.value().local_transforms);
                    auto to = mesh_rotations(target, second.value().local_transforms);
                    auto desired = to;
                    for(const auto& sample : samples) {
                        const auto first = from[sample.first] * inverse_rest[sample.first];
                        const auto last = from[sample.last] * inverse_rest[sample.last];
                        const auto delta = Math::normalize(Math::slerp(first, last, sample.alpha));
                        desired[sample.node] = Math::normalize(Math::slerp(to[sample.node],
                            delta * target_rest[sample.node].rotation, sample.weight));
                    }
                    // 按目标父序重建局部旋转，父链与子链可共同受原有躯干／根运动驱动。
                    for(auto node : target.evaluation_order) {
                        const auto parent = target.nodes[node].parent;
                        if(channel_for_node[node] >= 0) {
                            auto local = desired[node];
                            if(parent != NO_ANIMATION_NODE)
                                local = Math::inverse(to[parent]) * local;
                            else
                                local = Math::inverse(target_rest.back().rotation) * local;
                            local = Math::normalize(local);
                            auto& values = baked[channel_for_node[node]].values;
                            if(!values.empty()
                                && Math::dot(local, Math::Quat(values.back().w, values.back().x,
                                                        values.back().y, values.back().z))
                                       < 0)
                                local = -local;
                            values.push_back({local.x, local.y, local.z, local.w});
                            to[node] = desired[node];
                        } else if(parent != NO_ANIMATION_NODE)
                            to[node] = to[parent] * second.value().local_transforms[node].rotation;
                    }
                }
                auto& output = target.clips[clip_index].channels;
                output.insert(output.end(), std::make_move_iterator(baked.begin()),
                    std::make_move_iterator(baked.end()));
            }
            return Result<void>::success();
        }
        bool channel_moves(const AnimationChannel& channel, const JointTransform& rest) {
            const bool cubic =
                channel.interpolation == AnimationChannel::Interpolation::CubicSpline;
            for(std::size_t i = 0; i < channel.values.size(); ++i) {
                const auto& value = channel.values[i];
                if(cubic && i % 3 != 1) {
                    if(Math::length(value) > .0001f)
                        return true;
                    continue;
                }
                if(channel.path == AnimationChannel::Path::Rotation) {
                    const auto rotation =
                        Math::normalize(Math::Quat(value.w, value.x, value.y, value.z));
                    if(std::abs(Math::dot(rotation, rest.rotation)) < .99999f)
                        return true;
                } else {
                    const auto expected = channel.path == AnimationChannel::Path::Scale
                                              ? rest.scale
                                              : rest.translation;
                    if(Math::length(Math::Vec3(value) - expected) > .0001f)
                        return true;
                }
            }
            return false;
        }
    }
    Result<std::map<std::string, std::string>> suggest_bone_mapping(const AnimationData& source,
        const AnimationData& target, const std::map<std::string, std::string>& existing) {
        using Matched = Result<std::map<std::string, std::string>>;
        if(auto valid = validate_animation(source); !valid)
            return Matched::failure(valid.error());
        if(auto valid = validate_animation(target); !valid)
            return Matched::failure(valid.error());
        auto from_names = named_nodes(source), to_names = named_nodes(target);
        if(!from_names || !to_names)
            return Matched::failure(!from_names ? from_names.error() : to_names.error());
        std::map<std::string, std::vector<std::string>> from, to;
        for(const auto& joint : source.joints)
            from[bone_semantic(source.nodes[joint.node].name)].push_back(
                source.nodes[joint.node].name);
        for(const auto& joint : target.joints)
            to[bone_semantic(target.nodes[joint.node].name)].push_back(
                target.nodes[joint.node].name);
        auto output = existing;
        std::set<std::string> used;
        for(const auto& [_, name] : existing)
            used.insert(name);
        for(const auto& [semantic, names] : from) {
            const auto found = to.find(semantic);
            if(semantic.empty() || names.size() != 1 || found == to.end()
                || found->second.size() != 1 || output.contains(names.front())
                || used.contains(found->second.front()))
                continue;
            output.emplace(names.front(), found->second.front());
            used.insert(found->second.front());
        }
        return Matched::success(std::move(output));
    }
    Result<AnimationPose> retarget_reference_pose(
        const AnimationData& data, const std::map<std::string, Math::Vec3>& corrections) {
        if(auto valid = validate_animation(data); !valid)
            return Result<AnimationPose>::failure(valid.error());
        auto names = named_nodes(data);
        if(!names)
            return Result<AnimationPose>::failure(names.error());
        auto local = reference_transforms(data, names.value(), corrections);
        if(!local)
            return Result<AnimationPose>::failure(local.error());
        return build_animation_pose(data, std::move(local).value());
    }
    Result<void> AnimationRetarget::validate() const {
        if(nodes.size() > 4096 || translations.size() > 128 || chains.size() > 32
            || !std::isfinite(translation_scale) || translation_scale < 0)
            return Result<void>::failure("Invalid retarget settings");
        for(const auto& chain : chains)
            if(chain.source_root.empty() || chain.source_tip.empty() || chain.target_root.empty()
                || chain.target_tip.empty() || chain.source_root == chain.source_tip
                || chain.target_root == chain.target_tip || !std::isfinite(chain.weight)
                || chain.weight < 0 || chain.weight > 1)
                return Result<void>::failure("Invalid retarget chain endpoints or weight");
        for(const auto* pose : {&source_pose, &target_pose}) {
            if(pose->size() > 256)
                return Result<void>::failure("Too many retarget pose corrections");
            for(const auto& [name, degrees] : *pose)
                if(name.empty() || !Math::is_finite(degrees)
                    || Math::any_greater(Math::abs(degrees), Math::Vec3(180)))
                    return Result<void>::failure("Invalid reference pose correction: " + name);
        }
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
        auto source_rest =
            calibrated_rest_frames(source, source_names.value(), settings.source_pose);
        auto target_rest =
            calibrated_rest_frames(target, target_names.value(), settings.target_pose);
        if(!source_rest)
            return Bound::failure(source_rest.error());
        if(!target_rest)
            return Bound::failure(target_rest.error());
        std::vector<RetargetChain> chains;
        std::vector<bool> source_chained(source.nodes.size()), target_chained(target.nodes.size());
        for(const auto& chain : settings.chains) {
            auto from =
                chain_nodes(source, source_names.value(), chain.source_root, chain.source_tip);
            auto to =
                chain_nodes(target, target_names.value(), chain.target_root, chain.target_tip);
            if(!from || !to)
                return Bound::failure(!from ? from.error() : to.error());
            auto first = chain_distances(from.value(), source_rest.value());
            auto second = chain_distances(to.value(), target_rest.value());
            if(!first || !second)
                return Bound::failure(!first ? first.error() : second.error());
            for(auto node : from.value()) {
                if(source_chained[node])
                    return Bound::failure("Source retarget chains overlap");
                source_chained[node] = true;
            }
            for(auto node : to.value()) {
                if(target_chained[node])
                    return Bound::failure("Target retarget chains overlap");
                target_chained[node] = true;
            }
            chains.push_back({std::move(from).value(), std::move(to).value(),
                std::move(first).value(), std::move(second).value(), chain.weight});
        }
        std::vector<uint32_t> mapping(source.nodes.size(), NO_ANIMATION_NODE);
        std::vector<uint32_t> reverse(target.nodes.size(), NO_ANIMATION_NODE);
        for(uint32_t i = 0; i < source.nodes.size(); ++i) {
            if(source_chained[i])
                continue;
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
            if(destination == NO_ANIMATION_NODE || target_chained[destination])
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
        if(!mapped_joint && chains.empty())
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
        float scale = settings.translation_scale;
        if(scale == 0) {
            std::vector<float> ratios;
            for(const auto& chain : chains) {
                float first = 0, second = 0;
                for(size_t i = 1; i < chain.source.size(); ++i)
                    first += Math::length(source_rest.value()[chain.source[i]].position
                                          - source_rest.value()[chain.source[i - 1]].position);
                for(size_t i = 1; i < chain.target.size(); ++i)
                    second += Math::length(target_rest.value()[chain.target[i]].position
                                           - target_rest.value()[chain.target[i - 1]].position);
                ratios.push_back(second / first);
            }
            for(uint32_t i = 0; i < source.nodes.size(); ++i) {
                if(mapping[i] == NO_ANIMATION_NODE)
                    continue;
                const auto parent = mapped_parent(source, i, mapping);
                if(parent == NO_ANIMATION_NODE)
                    continue;
                const auto source_length = Math::length(
                    source_rest.value()[i].position - source_rest.value()[parent].position);
                const auto target_length =
                    Math::length(target_rest.value()[mapping[i]].position
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
                if(source_chained[from]) {
                    if(channel.path != AnimationChannel::Path::Rotation
                        && channel_moves(channel, source.nodes[from].rest))
                        return Bound::failure(
                            "Chain retargeting keeps target translations and scales: "
                            + source.nodes[from].name);
                    continue;
                }
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
                const auto pre = Math::inverse(second.rotation) * first.rotation;
                const auto post = Math::inverse(source_rest.value()[from].rotation)
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
                            rotation = Math::normalize(rotation);
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
            if(!settings.source_pose.empty() || !settings.target_pose.empty()) {
                for(uint32_t from = 0; from < mapping.size(); ++from) {
                    const auto to = mapping[from];
                    if(to == NO_ANIMATION_NODE
                        || std::ranges::any_of(converted.channels, [to](const auto& channel) {
                               return channel.node == to
                                      && channel.path == AnimationChannel::Path::Rotation;
                           }))
                        continue;
                    const auto parent_from = source.nodes[from].parent,
                               parent_to = target.nodes[to].parent;
                    const auto& first =
                        source_rest.value()[parent_from == NO_ANIMATION_NODE ? source.nodes.size()
                                                                             : parent_from];
                    const auto& second =
                        target_rest.value()[parent_to == NO_ANIMATION_NODE ? target.nodes.size()
                                                                           : parent_to];
                    const auto q =
                        Math::normalize(Math::inverse(second.rotation) * first.rotation
                                        * source.nodes[from].rest.rotation
                                        * Math::inverse(source_rest.value()[from].rotation)
                                        * target_rest.value()[to].rotation);
                    converted.channels.push_back({to, AnimationChannel::Path::Rotation,
                        AnimationChannel::Interpolation::Linear, {0}, {{q.x, q.y, q.z, q.w}}});
                }
            }
            output.clips.push_back(std::move(converted));
        }
        if(auto baked =
                bake_chains(source, output, chains, source_rest.value(), target_rest.value());
            !baked)
            return Bound::failure(baked.error());
        if(auto valid = validate_animation(output); !valid)
            return Bound::failure(valid.error());
        return Bound::success(std::move(output));
    }
}
