#include "animation/animation_data.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Comet {
    namespace {
        bool finite_matrix(const Math::Mat4& matrix) {
            for(int column = 0; column < 4; ++column)
                if(!Math::is_finite(matrix[column]))
                    return false;
            return true;
        }
        glm::quat quaternion(const Math::Vec4& value) {
            return glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
        }
        Math::Vec4 sample_channel(const AnimationChannel& channel, float time) {
            const auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time);
            const auto first =
                upper == channel.times.begin() ? 0 : std::size_t(upper - channel.times.begin() - 1);
            const auto second = std::min(first + 1, channel.times.size() - 1);
            const bool cubic =
                channel.interpolation == AnimationChannel::Interpolation::CubicSpline;
            const auto& start = channel.values[first * (cubic ? 3 : 1) + (cubic ? 1 : 0)];
            if(first == second || channel.interpolation == AnimationChannel::Interpolation::Step)
                return start;
            const float duration = channel.times[second] - channel.times[first];
            const float t = std::clamp((time - channel.times[first]) / duration, 0.0f, 1.0f);
            const auto& end = channel.values[second * (cubic ? 3 : 1) + (cubic ? 1 : 0)];
            if(cubic) {
                const float t2 = t * t, t3 = t2 * t;
                return (2 * t3 - 3 * t2 + 1) * start
                       + (t3 - 2 * t2 + t) * duration * channel.values[first * 3 + 2]
                       + (-2 * t3 + 3 * t2) * end
                       + (t3 - t2) * duration * channel.values[second * 3];
            }
            if(channel.path == AnimationChannel::Path::Rotation) {
                const auto q = glm::slerp(quaternion(start), quaternion(end), t);
                return {q.x, q.y, q.z, q.w};
            }
            return glm::mix(start, end, t);
        }
        std::vector<JointTransform> rest_pose(const AnimationData& data) {
            std::vector<JointTransform> rest;
            rest.reserve(data.nodes.size());
            for(const auto& node : data.nodes)
                rest.push_back(node.rest);
            return rest;
        }

        Result<std::vector<JointTransform>> local_pose(const AnimationData& data,
            const std::string& name, double time, bool loop, bool normalized_time = false) {
            auto pose = rest_pose(data);
            if(name.empty())
                return Result<std::vector<JointTransform>>::success(std::move(pose));
            const auto* clip = data.find_clip(name);
            if(!clip)
                return Result<std::vector<JointTransform>>::failure(
                    "Animation clip is missing: " + name);
            if(normalized_time)
                time *= clip->duration;
            const float sample_time = float(animation_sample_time(time, clip->duration, loop));
            for(const auto& channel : clip->channels) {
                const auto value = sample_channel(channel, sample_time);
                auto& node = pose[channel.node];
                switch(channel.path) {
                    case AnimationChannel::Path::Translation:
                        node.translation = value;
                        break;
                    case AnimationChannel::Path::Rotation:
                        node.rotation = quaternion(value);
                        break;
                    case AnimationChannel::Path::Scale:
                        node.scale = value;
                        break;
                }
            }
            return Result<std::vector<JointTransform>>::success(std::move(pose));
        }

        JointTransform mix_transform(
            const JointTransform& first, const JointTransform& second, float weight) {
            return {glm::mix(first.translation, second.translation, weight),
                glm::normalize(glm::slerp(first.rotation, second.rotation, weight)),
                glm::mix(first.scale, second.scale, weight)};
        }
        void blend_pose(std::vector<JointTransform>& first,
            const std::vector<JointTransform>& second, float weight) {
            for(std::size_t index = 0; index < first.size(); ++index)
                first[index] = mix_transform(first[index], second[index], weight);
        }

        Result<float> base_weight(const AnimationBlend& sample) {
            double total = 0;
            for(const auto& part : sample.blends) {
                if(!std::isfinite(part.weight) || part.weight < 0 || part.weight > 1)
                    return Result<float>::failure("Invalid animation blend weight");
                total += part.weight;
            }
            if(total > 1.00001 || sample.blends.size() > 64 || !std::isfinite(sample.time))
                return Result<float>::failure("Invalid animation blend");
            return Result<float>::success(float(std::max(0.0, 1 - total)));
        }
        Result<std::vector<JointTransform>> blended_pose(
            const AnimationData& data, const AnimationBlend& sample) {
            auto base = base_weight(sample);
            if(!base)
                return Result<std::vector<JointTransform>>::failure(base.error());
            auto pose = local_pose(data, base.value() > 0 ? sample.clip : std::string{},
                sample.time, sample.loop, sample.normalized_time);
            if(!pose)
                return pose;
            float accumulated = base.value();
            for(const auto& part : sample.blends) {
                if(part.weight == 0)
                    continue;
                auto next =
                    local_pose(data, part.clip, sample.time, sample.loop, sample.normalized_time);
                if(!next)
                    return next;
                accumulated += part.weight;
                blend_pose(pose.value(), next.value(), part.weight / accumulated);
            }
            return pose;
        }
        Result<void> apply_layer(const AnimationData& data, const AnimationLayer& layer,
            std::vector<JointTransform>& pose) {
            if(!std::isfinite(layer.weight) || layer.weight < 0 || layer.weight > 1
                || (layer.mode != AnimationLayer::Mode::Override
                    && layer.mode != AnimationLayer::Mode::Additive)
                || !std::isfinite(layer.reference_time) || layer.reference_time < 0)
                return Result<void>::failure("Invalid animation layer weight");
            std::vector<bool> included(data.nodes.size(), layer.mask.empty());
            for(const auto& name : layer.mask) {
                const auto found = std::ranges::find(data.nodes, name, &AnimationNode::name);
                if(found == data.nodes.end()
                    || std::count_if(data.nodes.begin(), data.nodes.end(), [&](const auto& node) {
                           return node.name == name;
                       }) != 1)
                    return Result<void>::failure("Missing or ambiguous mask node: " + name);
                included[std::size_t(found - data.nodes.begin())] = true;
            }
            if(layer.weight == 0)
                return Result<void>::success();
            auto overlay = blended_pose(data, layer.sample);
            if(!overlay)
                return Result<void>::failure(overlay.error());
            std::vector<JointTransform> reference;
            if(layer.mode == AnimationLayer::Mode::Additive) {
                auto sampled = local_pose(data, layer.reference_clip, layer.reference_time, false);
                if(!sampled)
                    return Result<void>::failure(sampled.error());
                reference = std::move(sampled).value();
            }
            for(const auto index : data.evaluation_order) {
                const auto parent = data.nodes[index].parent;
                if(parent != NO_ANIMATION_NODE && included[parent])
                    included[index] = true;
                if(!included[index])
                    continue;
                auto& node = pose[index];
                const auto& other = overlay.value()[index];
                if(layer.mode == AnimationLayer::Mode::Override) {
                    node = mix_transform(node, other, layer.weight);
                    continue;
                }
                const auto& rest = reference[index];
                if(glm::any(glm::lessThanEqual(glm::abs(rest.scale), Math::Vec3(1e-6f))))
                    return Result<void>::failure(
                        "Additive reference has a zero scale: " + data.nodes[index].name);
                node.translation += (other.translation - rest.translation) * layer.weight;
                const auto delta = glm::inverse(rest.rotation) * other.rotation;
                node.rotation = glm::normalize(
                    node.rotation * glm::slerp(Math::Quat(1, 0, 0, 0), delta, layer.weight));
                node.scale *= glm::mix(Math::Vec3(1), other.scale / rest.scale, layer.weight);
            }
            return Result<void>::success();
        }

        bool contains_node(const AnimationData& data, uint32_t parent, uint32_t node) {
            while(node != NO_ANIMATION_NODE) {
                if(node == parent)
                    return true;
                node = data.nodes[node].parent;
            }
            return false;
        }

        Result<uint32_t> motion_node(const AnimationData& data, std::string_view name) {
            if(data.joints.empty() || data.mesh_node >= data.nodes.size())
                return Result<uint32_t>::failure("Root motion requires a skinned mesh");
            uint32_t index = NO_ANIMATION_NODE;
            if(!name.empty()) {
                for(uint32_t i = 0; i < data.nodes.size(); ++i)
                    if(data.nodes[i].name == name) {
                        if(index != NO_ANIMATION_NODE)
                            return Result<uint32_t>::failure(
                                "Root motion node is ambiguous: " + std::string(name));
                        index = i;
                    }
            } else {
                index = data.joints.front().node;
                while(index != NO_ANIMATION_NODE
                      && !std::ranges::all_of(data.joints, [&](const auto& joint) {
                             return contains_node(data, index, joint.node);
                         }))
                    index = data.nodes[index].parent;
            }
            if(index == NO_ANIMATION_NODE || contains_node(data, index, data.mesh_node)
                || !std::ranges::all_of(data.joints,
                    [&](const auto& joint) { return contains_node(data, index, joint.node); }))
                return Result<uint32_t>::failure(
                    "Root motion needs one skeleton root outside the mesh node: "
                    + std::string(name));
            return Result<uint32_t>::success(index);
        }

        Math::Mat4 node_world(
            const AnimationData& data, const std::vector<JointTransform>& local, uint32_t index) {
            Math::Mat4 matrix(1);
            while(index != NO_ANIMATION_NODE) {
                const auto& node = local[index];
                matrix = glm::translate(Math::Mat4(1), node.translation)
                         * glm::mat4_cast(node.rotation) * glm::scale(Math::Mat4(1), node.scale)
                         * matrix;
                index = data.nodes[index].parent;
            }
            return matrix;
        }

        Math::Mat4 motion_frame(const AnimationData& data, const std::vector<JointTransform>& local,
            uint32_t index, const AnimationRootMotion& settings, const Math::Mat4& mesh_basis) {
            const auto root = mesh_basis
                              * glm::affineInverse(node_world(data, local, data.mesh_node))
                              * node_world(data, local, index);
            Math::Vec3 translation(root[3]);
            if(!settings.horizontal)
                translation.x = translation.z = 0;
            if(!settings.vertical)
                translation.y = 0;
            float yaw = 0;
            if(settings.rotation) {
                if(root[2].x * root[2].x + root[2].z * root[2].z > 1e-12f)
                    yaw = std::atan2(root[2].x, root[2].z);
                else
                    yaw = std::atan2(-root[0].z, root[0].x);
            }
            return glm::translate(Math::Mat4(1), translation)
                   * glm::rotate(Math::Mat4(1), yaw, Math::Vec3(0, 1, 0));
        }
    }

    Result<AnimationMotion> animation_root_motion(const AnimationData& data,
        const AnimationBlend& sample, double to, const AnimationRootMotion& settings) {
        using Outcome = Result<AnimationMotion>;
        if(!std::isfinite(to) || !std::isfinite(sample.time) || sample.time < 0 || to < sample.time)
            return Outcome::failure("Invalid root motion time interval");
        auto index = motion_node(data, settings.node);
        if(!index)
            return Outcome::failure(index.error());
        auto base = base_weight(sample);
        if(!base)
            return Outcome::failure(base.error());
        const auto rest = rest_pose(data);
        const auto mesh_basis = node_world(data, rest, data.mesh_node);
        const auto mesh_inverse = glm::affineInverse(mesh_basis);
        const auto reference = motion_frame(data, rest, index.value(), settings, mesh_basis);
        AnimationMotion motion;
        motion.up = Math::normalize(Math::Vec3(mesh_inverse * Math::Vec4(0, 1, 0, 0)));
        const auto accumulate = [&](const std::string& name, float weight) -> Result<void> {
            if(weight == 0 || name.empty())
                return Result<void>::success();
            const auto* clip = data.find_clip(name);
            if(!clip)
                return Result<void>::failure("Animation clip is missing: " + name);
            if(clip->duration <= 0)
                return Result<void>::success();
            const auto at = [&](double time) -> Result<Math::Mat4> {
                auto local = local_pose(data, name, time, false);
                if(!local)
                    return Result<Math::Mat4>::failure(local.error());
                return Result<Math::Mat4>::success(
                    motion_frame(data, local.value(), index.value(), settings, mesh_basis));
            };
            auto first = at(0);
            auto last = at(clip->duration);
            if(!first || !last)
                return Result<void>::failure(!first ? first.error() : last.error());
            const auto unwrapped = [&](double time) -> Result<Math::Mat4> {
                if(sample.normalized_time)
                    time *= clip->duration;
                uint32_t cycles = 0;
                if(sample.loop) {
                    const auto count = std::floor(time / clip->duration);
                    if(count > 1048576)
                        return Result<Math::Mat4>::failure("Root motion cycle limit exceeded");
                    cycles = static_cast<uint32_t>(count);
                    time = std::fmod(time, clip->duration);
                }
                auto phase = at(time);
                if(!phase)
                    return phase;
                auto cycle = glm::affineInverse(first.value()) * last.value();
                Math::Mat4 accumulated(1);
                while(cycles) {
                    if(cycles & 1u)
                        accumulated *= cycle;
                    cycles >>= 1;
                    if(cycles)
                        cycle *= cycle;
                }
                return Result<Math::Mat4>::success(first.value() * accumulated
                                                   * glm::affineInverse(first.value())
                                                   * phase.value());
            };
            auto from = unwrapped(sample.time);
            auto end = unwrapped(to);
            if(!from || !end)
                return Result<void>::failure(!from ? from.error() : end.error());
            const auto delta = reference * glm::affineInverse(from.value()) * end.value()
                               * glm::affineInverse(reference);
            if(!finite_matrix(delta))
                return Result<void>::failure("Root motion produced a non-finite transform");
            motion.translation += Math::Vec3((mesh_inverse * delta * mesh_basis)[3]) * weight;
            motion.yaw += std::atan2(delta[2].x, delta[2].z) * weight;
            return Result<void>::success();
        };
        if(auto added = accumulate(sample.clip, base.value()); !added)
            return Outcome::failure(added.error());
        for(const auto& part : sample.blends)
            if(auto added = accumulate(part.clip, part.weight); !added)
                return Outcome::failure(added.error());
        return Outcome::success(motion);
    }

    double animation_sample_time(double time, double duration, bool loop) {
        if(duration <= 0)
            return 0;
        if(loop)
            return std::fmod(std::max(time, 0.0), duration);
        return std::clamp(time, 0.0, duration);
    }

    Result<AnimationBlend> blend_animation_1d(
        std::span<const AnimationBlendPoint> points, float parameter) {
        if(points.empty() || !std::isfinite(parameter))
            return Result<AnimationBlend>::failure(
                "Blend space needs points and a finite parameter");
        for(std::size_t i = 0; i < points.size(); ++i)
            if(!std::isfinite(points[i].threshold)
                || (i > 0 && points[i - 1].threshold >= points[i].threshold))
                return Result<AnimationBlend>::failure(
                    "Blend space thresholds must strictly increase");
        const auto upper = std::upper_bound(points.begin(), points.end(), parameter,
            [](float value, const auto& point) { return value < point.threshold; });
        AnimationBlend result{.normalized_time = true};
        if(upper == points.begin())
            result.clip = points.front().clip;
        else if(upper == points.end())
            result.clip = points.back().clip;
        else {
            const auto& lower = *(upper - 1);
            result.clip = lower.clip;
            result.blends.push_back(
                {upper->clip, float((double(parameter) - lower.threshold)
                                    / (double(upper->threshold) - lower.threshold))});
        }
        return Result<AnimationBlend>::success(std::move(result));
    }

    Result<AnimationBlend> blend_animation_2d(
        std::span<const AnimationBlendPoint> points, Math::Vec2 parameter) {
        using ResultType = Result<AnimationBlend>;
        if(points.empty() || points.size() > 64 || !Math::is_finite(parameter))
            return ResultType::failure("2D blend needs 1..64 points and finite parameters");
        std::vector<double> weights(points.size(), 1);
        double total = 0;
        // 每对采样点定义一条线性权重带；归一化后支持不规则、共线及边界外输入。
        for(std::size_t i = 0; i < points.size(); ++i) {
            const auto& a = points[i];
            if(!std::isfinite(a.threshold) || !std::isfinite(a.y))
                return ResultType::failure("Non-finite 2D blend point");
            for(std::size_t j = 0; j < points.size(); ++j) {
                if(i == j)
                    continue;
                const auto& b = points[j];
                const double x = double(b.threshold) - a.threshold, y = double(b.y) - a.y;
                const double distance = x * x + y * y;
                if(!std::isfinite(distance) || distance < 1e-12)
                    return ResultType::failure("2D blend points must be distinct");
                const double projection =
                    ((double(parameter.x) - a.threshold) * x + (double(parameter.y) - a.y) * y)
                    / distance;
                weights[i] = std::min(weights[i], std::clamp(1 - projection, 0.0, 1.0));
            }
            total += weights[i];
        }
        if(total <= 0)
            return ResultType::failure("Cannot resolve 2D blend weights");
        AnimationBlend result{.normalized_time = true};
        bool first = true;
        for(std::size_t i = 0; i < points.size(); ++i) {
            if(weights[i] <= 0)
                continue;
            if(first) {
                result.clip = points[i].clip;
                first = false;
            } else
                result.blends.push_back({points[i].clip, float(weights[i] / total)});
        }
        return ResultType::success(std::move(result));
    }
    Result<float> animation_duration(const AnimationData& data, const AnimationBlend& sample) {
        auto base = base_weight(sample);
        if(!base)
            return base;
        float duration = 0, total = 0;
        const auto accumulate = [&](const std::string& name, float weight) -> Result<void> {
            if(weight == 0 || name.empty())
                return Result<void>::success();
            const auto* clip = data.find_clip(name);
            if(!clip)
                return Result<void>::failure("Animation clip is missing: " + name);
            // 静态片段不缩短运动片段的周期。
            if(clip->duration > 0) {
                duration += clip->duration * weight;
                total += weight;
            }
            return Result<void>::success();
        };
        if(auto status = accumulate(sample.clip, base.value()); !status)
            return Result<float>::failure(status.error());
        for(const auto& part : sample.blends)
            if(auto status = accumulate(part.clip, part.weight); !status)
                return Result<float>::failure(status.error());
        return Result<float>::success(total > 0 ? duration / total : 0);
    }
    const AnimationClip* AnimationData::find_clip(const std::string_view name) const {
        for(const auto& clip : clips)
            if(clip.name == name)
                return &clip;
        return nullptr;
    }
    Result<void> validate_animation(const AnimationData& data) {
        if(data.nodes.empty() || data.joints.empty() || data.mesh_node >= data.nodes.size()
            || data.evaluation_order.size() != data.nodes.size())
            return Result<void>::failure("Invalid skeleton nodes");
        std::vector<bool> visited(data.nodes.size());
        for(const auto index : data.evaluation_order) {
            if(index >= visited.size() || visited[index])
                return Result<void>::failure("Invalid skeleton evaluation order");
            const auto& node = data.nodes[index];
            if(node.parent != NO_ANIMATION_NODE
                && (node.parent >= visited.size() || !visited[node.parent]))
                return Result<void>::failure("Invalid skeleton hierarchy");
            if(!Math::is_finite(node.rest.translation) || !Math::is_finite(node.rest.scale)
                || !Math::is_finite(Math::Vec4(node.rest.rotation.x, node.rest.rotation.y,
                    node.rest.rotation.z, node.rest.rotation.w))
                || glm::length(node.rest.rotation) < 0.0001f)
                return Result<void>::failure("Invalid skeleton rest pose");
            visited[index] = true;
        }
        for(const auto& joint : data.joints)
            if(joint.node >= data.nodes.size() || !finite_matrix(joint.inverse_bind))
                return Result<void>::failure("Invalid skin joint");
        std::unordered_set<std::string> names;
        for(const auto& clip : data.clips) {
            if(clip.name.empty() || !names.insert(clip.name).second || !std::isfinite(clip.duration)
                || clip.duration < 0)
                return Result<void>::failure("Invalid animation clip");
            for(const auto& channel : clip.channels) {
                const bool cubic =
                    channel.interpolation == AnimationChannel::Interpolation::CubicSpline;
                if(channel.node >= data.nodes.size() || channel.times.empty()
                    || channel.values.size() != channel.times.size() * (cubic ? 3 : 1))
                    return Result<void>::failure("Invalid animation channel");
                float previous = -1;
                for(const auto time : channel.times) {
                    if(!std::isfinite(time) || time < 0 || time <= previous || time > clip.duration)
                        return Result<void>::failure("Invalid animation key times");
                    previous = time;
                }
                for(const auto& value : channel.values)
                    if(!Math::is_finite(value))
                        return Result<void>::failure("Non-finite animation key");
                if(channel.path == AnimationChannel::Path::Rotation)
                    for(std::size_t index = 0; index < channel.times.size(); ++index)
                        if(glm::length(channel.values[index * (cubic ? 3 : 1) + (cubic ? 1 : 0)])
                            < 0.0001f)
                            return Result<void>::failure("Invalid animation quaternion");
            }
        }
        return Result<void>::success();
    }
    Result<AnimationPose> build_animation_pose(const AnimationData& data,
        std::vector<JointTransform> local, const AnimationRootMotion* root_motion,
        bool include_bones) {
        if(local.size() != data.nodes.size() || data.mesh_node >= data.nodes.size())
            return Result<AnimationPose>::failure("Invalid local animation pose");
        Math::Mat4 motion_correction(1);
        if(root_motion) {
            const auto& settings = *root_motion;
            auto root = motion_node(data, settings.node);
            if(!root)
                return Result<AnimationPose>::failure(root.error());
            const auto rest = rest_pose(data);
            const auto basis = node_world(data, rest, data.mesh_node);
            motion_correction =
                glm::affineInverse(basis) * motion_frame(data, rest, root.value(), settings, basis)
                * glm::affineInverse(motion_frame(data, local, root.value(), settings, basis))
                * basis;
        }
        std::vector<Math::Mat4> world(data.nodes.size());
        for(const auto index : data.evaluation_order) {
            const auto& node = local[index];
            auto matrix = glm::translate(Math::Mat4(1), node.translation)
                          * glm::mat4_cast(node.rotation) * glm::scale(Math::Mat4(1), node.scale);
            if(data.nodes[index].parent != NO_ANIMATION_NODE)
                matrix = world[data.nodes[index].parent] * matrix;
            world[index] = matrix;
        }
        const auto mesh_inverse = glm::affineInverse(world[data.mesh_node]);
        AnimationPose output;
        std::optional<BoundingBox> bounds;
        output.matrices.reserve(data.joints.size());
        if(include_bones)
            output.bone_matrices.reserve(data.joints.size());
        for(const auto& joint : data.joints) {
            const auto bone = motion_correction * mesh_inverse * world[joint.node];
            const auto matrix = bone * joint.inverse_bind;
            if(!finite_matrix(matrix))
                return Result<AnimationPose>::failure(
                    "Animation produced a non-finite skin matrix");
            output.matrices.push_back(matrix);
            if(include_bones)
                output.bone_matrices.push_back(bone);
            if(joint.bounds) {
                const auto transformed = transform_box(*joint.bounds, matrix);
                if(!transformed)
                    return Result<AnimationPose>::failure("Animation produced invalid bounds");
                if(bounds)
                    bounds->include(*transformed);
                else
                    bounds = transformed;
            }
        }
        if(!bounds)
            return Result<AnimationPose>::failure("Skin has no weighted vertices");
        output.bounds = *bounds;
        output.local_transforms = std::move(local);
        return Result<AnimationPose>::success(std::move(output));
    }
    Result<AnimationPose> sample_animation(
        const AnimationData& data, const AnimationPlayback* playback, bool include_bones) {
        if(playback
            && (!std::isfinite(playback->blend) || playback->blend < 0 || playback->blend > 1))
            return Result<AnimationPose>::failure("Invalid animation playback time");
        if(playback && playback->clips && playback->clips_target.get() != &data)
            return Result<AnimationPose>::failure(
                "Animation library target changed; rebind required");
        if(playback && !playback->ik.empty() && playback->pose_source
            && playback->pose_source.get() != &data)
            return Result<AnimationPose>::failure("IK target skeleton changed; rebuild required");
        const auto& clips = playback && playback->clips ? *playback->clips : data;
        auto local = blended_pose(clips, playback ? playback->current : AnimationBlend{});
        if(!local)
            return Result<AnimationPose>::failure(local.error());
        if(playback && playback->blend < 1 && playback->previous.has_clip()) {
            auto previous = blended_pose(clips, playback->previous);
            if(!previous)
                return Result<AnimationPose>::failure(previous.error());
            blend_pose(local.value(), previous.value(), 1 - playback->blend);
        }
        if(playback)
            for(const auto& layer : playback->layers)
                if(auto status = apply_layer(clips, layer, local.value()); !status)
                    return Result<AnimationPose>::failure(status.error());
        const auto* root = playback && playback->root_motion ? &*playback->root_motion : nullptr;
        auto pose = build_animation_pose(data, std::move(local).value(), root,
            include_bones || (playback && !playback->ik.empty()));
        if(!pose || !playback || playback->ik.empty())
            return pose;
        return apply_animation_ik(data, std::move(pose).value(), playback->ik, root);
    }
    Result<AnimationPose> interpolate_animation_pose(const AnimationData& data,
        const AnimationPose& previous, const AnimationPose& current, float alpha,
        const AnimationRootMotion* root_motion) {
        if(!std::isfinite(alpha) || alpha < 0 || alpha > 1
            || previous.local_transforms.size() != data.nodes.size()
            || current.local_transforms.size() != data.nodes.size())
            return Result<AnimationPose>::failure("Invalid animation interpolation inputs");
        auto local = previous.local_transforms;
        blend_pose(local, current.local_transforms, alpha);
        return build_animation_pose(data, std::move(local), root_motion, true);
    }
    Result<AnimationPose> resolve_animation_pose(const AnimationData& data,
        const AnimationPlayback* playback, std::string& error, bool include_bones) {
        error.clear();
        auto pose = sample_animation(data, playback, include_bones);
        if(pose)
            return pose;
        error = pose.error();
        if(playback && playback->blend < 1 && playback->previous.has_clip()) {
            auto current = *playback;
            current.previous = {};
            current.blend = 1;
            pose = sample_animation(data, &current, include_bones);
        }
        return pose ? std::move(pose) : sample_animation(data, nullptr, include_bones);
    }
}
