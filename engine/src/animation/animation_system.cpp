#include "animation/animation_system.h"
#include "scene/scene.h"
#include <algorithm>
#include <cmath>
namespace Comet {
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
        if(state.parameter.empty()) {
            selection.sample = {.clip = state.motions.front().clip, .loop = state.loop};
        } else {
            selection.parameter =
                std::get<float>(*definition.parameter(state.parameter, animator.parameters));
            if(selection.state == playback.state && state.smoothing > 0) {
                const float weight = float(-std::expm1(-delta / state.smoothing));
                selection.parameter = std::lerp(playback.parameter, selection.parameter, weight);
            }
            auto mixed = blend_animation_1d(state.motions, selection.parameter);
            if(!mixed)
                return Selected::failure({mixed.error()});
            selection.sample = std::move(mixed).value();
            selection.sample.loop = state.loop;
        }
        return Selected::success(std::move(selection));
    }
    Result<void, Error> AnimationSystem::advance(Scene& scene, double delta) {
        Result<void, Error> result = Result<void, Error>::success();
        scene.each<const AnimatorComponent>([&](Entity entity, const AnimatorComponent& animator) {
            if(!result)
                return;
            if(!animator.enabled || !entity.has_component<MeshRendererComponent>()) {
                scene.set_animation_playback(entity, nullptr);
                m_playbacks.erase(entity.get_id());
                return;
            }
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
                playback.transition_time = 0;
                playback.progress = 0;
                playback.transition_duration = selection.value().transition;
            } else {
                // 同一混合空间跨阈值时保持周期相位，不把权重变化当作状态切换。
                next.time = frame.current.time;
            }
            playback.state = selection.value().state;
            playback.parameter = selection.value().parameter;
            frame.current = std::move(next);
            std::shared_ptr<const AnimationData> data;
            if(m_source)
                data = m_source(entity.get_component<MeshRendererComponent>().mesh);
            const auto advance_sample = [&](AnimationBlend& sample, bool current) {
                if(!sample.has_clip())
                    return;
                double step = delta * animator.speed;
                if(!data) {
                    if(!sample.normalized_time)
                        sample.time += step;
                    return;
                }
                const auto duration = animation_duration(*data, sample);
                // 缺失片段由姿态采样诊断；加载后再推进，避免累计无效时间。
                if(!duration)
                    return;
                double period = duration.value();
                if(current) {
                    // 完成条件独立于循环采样时间；每帧至多选择一次过渡。
                    playback.progress =
                        period > 0 ? std::min(playback.progress + step / period, 1.0) : 1.0;
                }
                if(sample.normalized_time) {
                    step = period > 0 ? step / period : 0;
                    period = period > 0 ? 1 : 0;
                }
                sample.time = animation_sample_time(sample.time + step, period, sample.loop);
            };
            advance_sample(frame.current, true);
            advance_sample(frame.previous, false);
            playback.transition_time += float(delta);
            frame.blend = 1;
            if(playback.transition_duration > 0 && frame.previous.has_clip())
                frame.blend =
                    std::min(playback.transition_time / playback.transition_duration, 1.0f);
            if(frame.blend == 1)
                frame.previous = {};
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
        return advance(scene, 0);
    }
    Result<void, Error> AnimationSystem::update(Scene& scene, const Context& context) {
        return advance(scene, context.delta_time);
    }
    void AnimationSystem::on_stop(Scene& scene, RuntimeSession&, const RuntimeServices&) noexcept {
        for(const auto& [id, playback] : m_playbacks)
            if(auto entity = scene.find_entity(id))
                scene.set_animation_playback(entity, nullptr);
        m_playbacks.clear();
    }
}
