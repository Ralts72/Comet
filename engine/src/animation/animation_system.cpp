#include "animation/animation_system.h"
#include "scene/scene.h"
#include "diagnostics/profiler.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
namespace Comet {
    void prepare_bone_attachment_sources(Scene& scene, const AnimationSystem::Source& source) {
        if(!source)
            return;
        std::unordered_set<EntityId> prepared;
        scene.each<const BoneAttachmentComponent>([&](Entity child, const auto&) {
            const auto parent = scene.get_parent(child);
            if(!parent || !parent.has_component<MeshRendererComponent>()
                || !prepared.insert(parent.get_id()).second)
                return;
            scene.set_animation_source(
                parent, source(parent.get_component<MeshRendererComponent>().mesh));
        });
    }

    Result<void, Error> AnimationSystem::emit_events(
        Scene& scene, EntityId id, Playback& playback, double from, double to) {
        if(!playback.controller || to <= from)
            return Result<void, Error>::success();
        const auto* state = playback.controller->find_state(playback.state);
        auto entity = scene.find_entity(id);
        const bool entering = !std::exchange(playback.events_started, true);
        if(state->events.empty())
            return Result<void, Error>::success();
        if(to - from > 1024)
            return Result<void, Error>::failure({"Animation event limit reached"});
        // 使用推进前后的区间；不从取模后的时间反推，跨多圈也不漏事件。
        constexpr double epsilon = std::numeric_limits<float>::epsilon();
        const auto last_cycle = state->loop ? std::floor(to + epsilon) : 0;
        for(double cycle = std::floor(from); cycle <= last_cycle; cycle += 1) {
            for(const auto& event : state->events) {
                const double time = cycle + event.phase;
                if(time > to + epsilon || (time <= from + epsilon && !(entering && time == 0)))
                    continue;
                if(!scene.emit_event(event.name, event.value, entity))
                    return Result<void, Error>::failure({"Animation event limit reached"});
            }
        }
        return Result<void, Error>::success();
    }
    Result<AnimationSystem::Selection, Error> AnimationSystem::select(
        const AnimatorComponent& animator, Playback& playback, double delta) {
        using Selected = Result<Selection, Error>;
        if(!animator.controller) {
            if(playback.controller)
                playback = {};
            return Selected::success({.sample = {.clip = animator.clip, .loop = animator.loop},
                .transition = animator.transition});
        }
        auto controller = m_controllers ? m_controllers(animator.controller) : nullptr;
        if(!controller)
            return Selected::failure({"Animation controller is not loaded"});
        if(playback.controller != controller) {
            if(auto valid = controller->validate(); !valid)
                return Selected::failure({valid.error()});
            playback = {};
            playback.controller = std::move(controller);
        }
        const auto& definition = *playback.controller;
        if(auto valid = definition.validate_parameters(animator.parameters); !valid)
            return Selected::failure({valid.error()});
        Selection selection;
        selection.state = playback.state.empty() ? definition.initial : playback.state;
        selection.transition = playback.transition_duration;
        if(const auto* transition = definition.next_transition(
               selection.state, animator.parameters, playback.progress >= 1)) {
            selection.state = transition->to;
            selection.transition = transition->duration;
        }
        const auto& state = *definition.find_state(selection.state);
        selection.parameter = definition.blend_parameters(state, animator.parameters);
        if(selection.state == playback.state && state.smoothing > 0) {
            const float weight = float(-std::expm1(-delta / state.smoothing));
            selection.parameter = glm::mix(playback.parameter, selection.parameter, weight);
        }
        auto mixed = definition.sample(state, selection.parameter);
        if(!mixed)
            return Selected::failure({mixed.error()});
        selection.sample = std::move(mixed).value();
        return Selected::success(std::move(selection));
    }
    Result<void, Error> AnimationSystem::advance(
        Scene& scene, double delta, const RuntimeServices& services, std::optional<bool> fixed) {
        PROFILE_SCOPE("AnimationSystem::advance");
        Result<void, Error> result = Result<void, Error>::success();
        std::unordered_set<EntityUuid> motion_targets;
        scene.each<const AnimatorComponent>([&](Entity entity, const AnimatorComponent& animator) {
            if(!result)
                return;
            if(!animator.enabled || !entity.has_component<MeshRendererComponent>()) {
                scene.set_animation_playback(entity, nullptr);
                m_playbacks.erase(entity.get_id());
                return;
            }
            const RootMotionComponent* root =
                entity.has_component<RootMotionComponent>()
                        && entity.get_component<RootMotionComponent>().enabled
                    ? &entity.get_component<RootMotionComponent>()
                    : nullptr;
            const bool foot_placement = entity.has_component<FootPlacementComponent>()
                                        && entity.get_component<FootPlacementComponent>().enabled;
            if(fixed && *fixed != (bool(root) || foot_placement))
                return;
            if(!std::isfinite(animator.speed) || animator.speed < 0
                || !std::isfinite(animator.transition) || animator.transition < 0) {
                result = Result<void, Error>::failure({"Invalid Animator playback settings"});
                return;
            }
            auto& playback = m_playbacks[entity.get_id()];
            auto selection = select(animator, playback, delta);
            if(!selection) {
                result = Result<void, Error>::failure(selection.error());
                return;
            }
            auto& frame = playback.frame;
            auto next = std::move(selection.value().sample);
            const bool changed = selection.value().state != playback.state
                                 || (!animator.controller && next.clip != frame.current.clip);
            if(changed) {
                frame.previous = frame.current;
                frame.previous_state = frame.state;
                playback.transition_time = 0;
                playback.progress = 0;
                playback.events_started = false;
                playback.transition_duration = selection.value().transition;
            } else {
                // 同一混合空间跨阈值时保持周期相位，不把权重变化当作状态切换。
                next.time = frame.current.time;
            }
            playback.state = selection.value().state;
            frame.state = playback.state;
            playback.parameter = selection.value().parameter;
            frame.current = std::move(next);
            std::shared_ptr<const AnimationData> data;
            if(m_source)
                data = m_source(entity.get_component<MeshRendererComponent>().mesh);
            if(m_source)
                scene.set_animation_source(entity, data);
            if(playback.controller && playback.controller->library && data) {
                auto library = m_libraries ? m_libraries(playback.controller->library) : nullptr;
                if(!library) {
                    result = Result<void, Error>::failure({"Animation library is not loaded"});
                    return;
                }
                if(playback.library != library || frame.clips_target != data) {
                    auto bound = library->bind(*data);
                    if(!bound) {
                        result = Result<void, Error>::failure({bound.error()});
                        return;
                    }
                    frame.clips = std::make_shared<const AnimationData>(std::move(bound).value());
                    frame.clips_target = data;
                    playback.library = std::move(library);
                }
            } else {
                frame.clips.reset();
                frame.clips_target.reset();
                playback.library.reset();
            }
            const auto clips = frame.clips ? frame.clips : data;
            frame.root_motion.reset();
            if(root) {
                if(!clips) {
                    result = Result<void, Error>::failure(
                        {"Root motion requires loaded animation data"});
                    return;
                }
                const auto target = root->target ? root->target : entity.get_uuid();
                if(!motion_targets.insert(target).second) {
                    result = Result<void, Error>::failure(
                        {"Multiple Animators drive the same root motion target"});
                    return;
                }
                frame.root_motion = AnimationRootMotion{
                    root->node, root->horizontal, root->vertical, root->rotation};
            }
            AnimationMotion current_motion, previous_motion;
            const double motion_from = frame.current.time;
            double motion_to = motion_from;
            const auto advance_sample = [&](AnimationBlend& sample, bool current,
                                            AnimationMotion* motion = nullptr) {
                if(!sample.has_clip())
                    return;
                double step = delta * animator.speed;
                if(!clips) {
                    if(!sample.normalized_time)
                        sample.time += step;
                    return;
                }
                const auto duration = animation_duration(*clips, sample);
                // 缺失片段由姿态采样诊断；加载后再推进，避免累计无效时间。
                if(!duration)
                    return;
                double period = duration.value();
                if(current && period > 0 && step > 0) {
                    const double phase =
                        sample.normalized_time ? sample.time : sample.time / period;
                    const double end =
                        sample.loop ? phase + step / period : std::min(phase + step / period, 1.0);
                    result = emit_events(scene, entity.get_id(), playback, phase, end);
                }
                if(current) {
                    // 完成条件独立于循环采样时间；每帧至多选择一次过渡。
                    playback.progress =
                        period > 0 ? std::min(playback.progress + step / period, 1.0) : 1.0;
                }
                if(sample.normalized_time) {
                    step = period > 0 ? step / period : 0;
                    period = period > 0 ? 1 : 0;
                }
                if(motion && frame.root_motion) {
                    if(current)
                        motion_to = sample.time + step;
                    auto extracted = animation_root_motion(
                        *clips, sample, sample.time + step, *frame.root_motion);
                    if(!extracted) {
                        result = Result<void, Error>::failure({extracted.error()});
                        return;
                    }
                    *motion = extracted.value();
                }
                sample.time = animation_sample_time(sample.time + step, period, sample.loop);
            };
            advance_sample(frame.current, true, &current_motion);
            advance_sample(frame.previous, false, &previous_motion);
            if(!result)
                return;
            const float transition_start = playback.transition_time;
            const bool had_previous = frame.previous.has_clip();
            playback.transition_time += float(delta);
            frame.blend = 1;
            if(playback.transition_duration > 0 && frame.previous.has_clip())
                frame.blend =
                    std::min(playback.transition_time / playback.transition_duration, 1.0f);
            if(root) {
                float weight = frame.blend;
                if(had_previous && playback.transition_duration > 0 && delta > 0) {
                    const double duration = playback.transition_duration;
                    const auto integral = [&](double time) {
                        return time < duration ? time * time / (2 * duration)
                                               : time - duration * .5;
                    };
                    weight = float(
                        (integral(playback.transition_time) - integral(transition_start)) / delta);
                }
                AnimationMotion motion{.translation = glm::mix(previous_motion.translation,
                                           current_motion.translation, weight),
                    .yaw = glm::mix(previous_motion.yaw, current_motion.yaw, weight),
                    .up = current_motion.up};
                if(!m_motion) {
                    result =
                        Result<void, Error>::failure({"Root motion requires a movement consumer"});
                    return;
                }
                result = m_motion(scene, entity, *root,
                    {*clips, frame.current, frame.state, motion_from, motion_to, motion}, delta,
                    services);
                if(!result)
                    return;
            }
            if(frame.blend == 1) {
                frame.previous = {};
                frame.previous_state.clear();
            }
            if(playback.controller) {
                const auto& definition = *playback.controller;
                const bool entering = frame.layers.size() != definition.layers.size();
                frame.layers.resize(definition.layers.size());
                playback.layer_parameters.resize(definition.layers.size());
                for(std::size_t i = 0; i < definition.layers.size(); ++i) {
                    const auto& layer = definition.layers[i];
                    const auto& state = *definition.find_state(layer.state);
                    auto value = definition.blend_parameters(state, animator.parameters);
                    if(!entering && state.smoothing > 0)
                        value = glm::mix(playback.layer_parameters[i], value,
                            float(-std::expm1(-delta / state.smoothing)));
                    playback.layer_parameters[i] = value;
                    auto sample = definition.sample_layer(layer, animator.parameters, value);
                    if(!sample) {
                        result = Result<void, Error>::failure({sample.error()});
                        return;
                    }
                    sample.value().sample.time = frame.layers[i].sample.time;
                    auto& output = frame.layers[i];
                    output = std::move(sample).value();
                    // 层有独立时钟，权重为零仍推进；事件只由基础状态发出。
                    advance_sample(output.sample, false);
                }
            } else
                frame.layers.clear();
            const auto published = scene.get_animation_playback(entity);
            if(!published || *published != frame)
                scene.set_animation_playback(
                    entity, std::make_shared<const AnimationPlayback>(frame));
        });
        std::erase_if(m_playbacks, [&](const auto& entry) {
            Entity entity = scene.find_entity(entry.first);
            if(entity && entity.has_component<AnimatorComponent>())
                return false;
            if(entity)
                scene.set_animation_playback(entity, nullptr);
            return true;
        });
        return result;
    }
    Result<void, Error> AnimationSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices& services) {
        m_playbacks.clear();
        return advance(scene, 0, services);
    }
    Result<void, Error> AnimationSystem::update(Scene& scene, const Context& context) {
        return advance(scene, context.delta_time, context.services, false);
    }
    Result<void, Error> AnimationSystem::fixed_update(Scene& scene, const Context& context) {
        return advance(scene, context.delta_time, context.services, true);
    }
    void AnimationSystem::on_stop(Scene& scene, RuntimeSession&, const RuntimeServices&) noexcept {
        for(const auto& [id, playback] : m_playbacks)
            if(auto entity = scene.find_entity(id))
                scene.set_animation_playback(entity, nullptr);
        m_playbacks.clear();
    }
}
