#include "animation/animation_system.h"
#include "scene/scene.h"
#include "gameplay/character_controller.h"
#include <algorithm>
#include <array>
namespace Comet {
    Result<AnimationSystem::Selection, Error> AnimationSystem::select(Scene& scene,
        const AnimatorComponent& animator, const RuntimeServices& services,
        const Playback& previous, double delta) {
        Selection selection;
        selection.sample = {.clip = animator.clip, .loop = animator.loop};
        if(!animator.character)
            return Result<Selection, Error>::success(std::move(selection));
        selection.sample = {.clip = animator.idle};
        auto actor = scene.find_entity(animator.character);
        if(!services.physics || !actor)
            return Result<Selection, Error>::success(std::move(selection));
        auto state = Gameplay::CharacterController::get_state(actor, *services.physics);
        if(!state)
            return Result<Selection, Error>::success(std::move(selection));
        using Motion = Gameplay::CharacterController::Motion;
        if(state.value().motion == Motion::Jumping || state.value().motion == Motion::Falling) {
            const bool jumping = state.value().motion == Motion::Jumping;
            selection.mode = jumping ? Mode::Jump : Mode::Fall;
            selection.sample = {.clip = jumping ? animator.jump : animator.fall, .loop = false};
            return Result<Selection, Error>::success(std::move(selection));
        }
        const std::array ground{AnimationBlendPoint{0, animator.idle},
            AnimationBlendPoint{animator.walk_speed, animator.walk},
            AnimationBlendPoint{animator.run_speed, animator.run}};
        const std::array crouch{AnimationBlendPoint{0, animator.crouch},
            AnimationBlendPoint{animator.crouch_speed, animator.crouch_walk}};
        const bool crouched = state.value().crouched;
        selection.mode = crouched ? Mode::Crouch : Mode::Ground;
        selection.movement_speed = state.value().speed;
        if(selection.mode == previous.mode && animator.transition > 0) {
            const float weight = float(-std::expm1(-delta / animator.transition));
            selection.movement_speed =
                std::lerp(previous.movement_speed, selection.movement_speed, weight);
        }
        std::span<const AnimationBlendPoint> points = ground;
        if(crouched)
            points = crouch;
        auto mixed = blend_animation_1d(points, selection.movement_speed);
        if(!mixed)
            return Result<Selection, Error>::failure({mixed.error()});
        selection.sample = std::move(mixed).value();
        return Result<Selection, Error>::success(std::move(selection));
    }
    Result<void, Error> AnimationSystem::advance(
        Scene& scene, double delta, const RuntimeServices& services) {
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
            auto selection = select(scene, animator, services, playback, delta);
            if(!selection) {
                result = Result<void, Error>::failure(selection.error());
                return;
            }
            auto& frame = playback.frame;
            auto next = std::move(selection.value().sample);
            const auto mode = selection.value().mode;
            const bool changed =
                mode != playback.mode || (!next.normalized_time && next.clip != frame.current.clip);
            if(changed) {
                frame.previous = frame.current;
                playback.transition_time = 0;
            } else {
                // 同一混合空间跨阈值时保持周期相位，不把权重变化当作状态切换。
                next.time = frame.current.time;
            }
            playback.mode = mode;
            playback.movement_speed = selection.value().movement_speed;
            frame.current = std::move(next);
            std::shared_ptr<const AnimationData> data;
            if(m_source)
                data = m_source(entity.get_component<MeshRendererComponent>().mesh);
            const auto advance_sample = [&](AnimationBlend& sample) {
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
                if(sample.normalized_time) {
                    step = period > 0 ? step / period : 0;
                    period = period > 0 ? 1 : 0;
                }
                sample.time = animation_sample_time(sample.time + step, period, sample.loop);
            };
            advance_sample(frame.current);
            advance_sample(frame.previous);
            playback.transition_time += float(delta);
            frame.blend = 1;
            if(animator.transition > 0 && frame.previous.has_clip())
                frame.blend = std::min(playback.transition_time / animator.transition, 1.0f);
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
        return advance(scene, 0, services);
    }
    Result<void, Error> AnimationSystem::update(Scene& scene, const Context& context) {
        return advance(scene, context.delta_time, context.services);
    }
    void AnimationSystem::on_stop(Scene& scene, RuntimeSession&, const RuntimeServices&) noexcept {
        for(const auto& [id, playback] : m_playbacks)
            if(auto entity = scene.find_entity(id))
                scene.set_animation_playback(entity, nullptr);
        m_playbacks.clear();
    }
}
