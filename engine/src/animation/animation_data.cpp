#include "animation/animation_data.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
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
        Result<std::vector<JointTransform>> local_pose(const AnimationData& data,
            const std::string& name, double time, bool loop, bool normalized_time = false) {
            std::vector<JointTransform> pose;
            pose.reserve(data.nodes.size());
            for(const auto& node : data.nodes)
                pose.push_back(node.rest);
            if(name.empty())
                return Result<std::vector<JointTransform>>::success(std::move(pose));
            const auto* clip = data.find_clip(name);
            if(!clip)
                return Result<std::vector<JointTransform>>::failure(
                    "Animation clip is missing: " + name);
            if(normalized_time)
                time *= clip->duration;
            float sample_time = std::clamp(float(time), 0.0f, clip->duration);
            if(loop && clip->duration > 0)
                sample_time = float(std::fmod(std::max(time, 0.0), clip->duration));
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

        void blend_pose(std::vector<JointTransform>& first,
            const std::vector<JointTransform>& second, float weight) {
            for(std::size_t index = 0; index < first.size(); ++index) {
                auto& node = first[index];
                const auto& other = second[index];
                node.translation = glm::mix(node.translation, other.translation, weight);
                node.rotation = glm::slerp(node.rotation, other.rotation, weight);
                node.scale = glm::mix(node.scale, other.scale, weight);
            }
        }

        Result<std::vector<JointTransform>> blended_pose(
            const AnimationData& data, const AnimationBlend& sample) {
            if(!std::isfinite(sample.time) || !std::isfinite(sample.weight) || sample.weight < 0
                || sample.weight > 1)
                return Result<std::vector<JointTransform>>::failure("Invalid animation blend");
            auto pose =
                local_pose(data, sample.clip, sample.time, sample.loop, sample.normalized_time);
            if(!pose || sample.weight == 0)
                return pose;
            auto second = local_pose(
                data, sample.second_clip, sample.time, sample.loop, sample.normalized_time);
            if(!second)
                return second;
            blend_pose(pose.value(), second.value(), sample.weight);
            return pose;
        }
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
            result.second_clip = upper->clip;
            result.weight = float((double(parameter) - lower.threshold)
                                  / (double(upper->threshold) - lower.threshold));
        }
        return Result<AnimationBlend>::success(std::move(result));
    }

    Result<float> animation_duration(const AnimationData& data, const AnimationBlend& sample) {
        if(!std::isfinite(sample.weight) || sample.weight < 0 || sample.weight > 1)
            return Result<float>::failure("Invalid animation blend weight");
        const auto* first = data.find_clip(sample.clip);
        if(!first && !sample.clip.empty())
            return Result<float>::failure("Animation clip is missing: " + sample.clip);
        const float first_duration = first ? first->duration : 0;
        if(sample.weight == 0)
            return Result<float>::success(first_duration);
        const auto* second = data.find_clip(sample.second_clip);
        if(!second && !sample.second_clip.empty())
            return Result<float>::failure("Animation clip is missing: " + sample.second_clip);
        const float second_duration = second ? second->duration : 0;
        // 绑定姿态／静态片段不缩短另一片段的周期。
        if(first_duration == 0 || second_duration == 0)
            return Result<float>::success(std::max(first_duration, second_duration));
        return Result<float>::success(glm::mix(first_duration, second_duration, sample.weight));
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
    Result<AnimationPose> sample_animation(
        const AnimationData& data, const AnimationPlayback* playback) {
        if(playback
            && (!std::isfinite(playback->blend) || playback->blend < 0 || playback->blend > 1))
            return Result<AnimationPose>::failure("Invalid animation playback time");
        auto local = blended_pose(data, playback ? playback->current : AnimationBlend{});
        if(!local)
            return Result<AnimationPose>::failure(local.error());
        if(playback && playback->blend < 1 && playback->previous.has_clip()) {
            auto previous = blended_pose(data, playback->previous);
            if(!previous)
                return Result<AnimationPose>::failure(previous.error());
            blend_pose(local.value(), previous.value(), 1 - playback->blend);
        }
        std::vector<Math::Mat4> world(data.nodes.size());
        for(const auto index : data.evaluation_order) {
            const auto& node = local.value()[index];
            auto matrix = glm::translate(Math::Mat4(1), node.translation)
                          * glm::mat4_cast(node.rotation) * glm::scale(Math::Mat4(1), node.scale);
            if(data.nodes[index].parent != NO_ANIMATION_NODE)
                matrix = world[data.nodes[index].parent] * matrix;
            world[index] = matrix;
        }
        const auto mesh_inverse = Math::inverse(world[data.mesh_node]);
        AnimationPose output;
        std::optional<BoundingBox> bounds;
        output.matrices.reserve(data.joints.size());
        for(const auto& joint : data.joints) {
            const auto matrix = mesh_inverse * world[joint.node] * joint.inverse_bind;
            if(!finite_matrix(matrix))
                return Result<AnimationPose>::failure(
                    "Animation produced a non-finite skin matrix");
            output.matrices.push_back(matrix);
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
        return Result<AnimationPose>::success(std::move(output));
    }
}
