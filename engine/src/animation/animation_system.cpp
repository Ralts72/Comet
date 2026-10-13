#include "animation/animation_system.h"
#include "scene/scene.h"
#include "gameplay/character_controller.h"
#include <algorithm>
namespace Comet {
    namespace {
        const std::string& selected_clip(
            Scene& scene, const AnimatorComponent& animator, const RuntimeServices& services) {
            if(!animator.character)
                return animator.clip;
            if(!services.physics)
                return animator.idle;
            auto actor = scene.find_entity(animator.character);
            auto state = Gameplay::CharacterController::get_state(actor, *services.physics);
            if(!state)
                return animator.idle;
            switch(state.value().motion) {
                case Gameplay::CharacterController::Motion::Idle:
                    return animator.idle;
                case Gameplay::CharacterController::Motion::Walking:
                    return animator.walk;
                case Gameplay::CharacterController::Motion::Running:
                    return animator.run;
                case Gameplay::CharacterController::Motion::Crouching:
                    if(state.value().speed > 0.05f)
                        return animator.crouch_walk;
                    return animator.crouch;
                case Gameplay::CharacterController::Motion::Jumping:
                    return animator.jump;
                case Gameplay::CharacterController::Motion::Falling:
                    return animator.fall;
            }
            return animator.idle;
        }
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
            auto& frame = playback.frame;
            const auto& clip = selected_clip(scene, animator, services);
            if(clip != frame.clip) {
                frame.previous_clip = frame.clip;
                frame.previous_time = frame.time;
                frame.previous_loop = frame.loop;
                frame.clip = clip;
                frame.time = 0;
                playback.transition_time = 0;
            }
            frame.loop = animator.loop;
            if(animator.character && (clip == animator.jump || clip == animator.fall))
                frame.loop = false;
            frame.time += delta * animator.speed;
            if(!frame.previous_clip.empty())
                frame.previous_time += delta * animator.speed;
            playback.transition_time += float(delta);
            frame.blend = 1;
            if(animator.transition > 0)
                frame.blend = std::min(playback.transition_time / animator.transition, 1.0f);
            if(frame.blend == 1)
                frame.previous_clip.clear();
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
