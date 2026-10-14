#include "scene/systems/audio_system.h"

#include "scene/scene.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace Comet {
    Result<void, Error> AudioSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices& services) {
        if(services.audio != &m_audio || !m_audio.is_bound_to(scene))
            return Result<void, Error>::failure(
                {"AudioSystem requires its audio service in RuntimeServices"});
        return synchronize(scene);
    }

    Result<void, Error> AudioSystem::update(Scene&, const Context& context) {
        return m_audio.advance(context.delta_time);
    }

    Result<void, Error> AudioSystem::post_update(Scene& scene, const Context&) {
        // 脚本与动画事件交付后再接纳声音，不提前消耗新请求的播放时长。
        return synchronize(scene);
    }

    Result<void, Error> AudioSystem::synchronize(Scene& scene) {
        scene.update_world_transforms();
        std::optional<AudioListenerPose> listener;
        bool duplicate = false;
        scene.each<const AudioListenerComponent>([&](Entity entity, const auto& component) {
            if(!component.enabled)
                return;
            if(listener) {
                duplicate = true;
                return;
            }
            const auto& transform = entity.get_component<WorldTransformComponent>();
            listener = AudioListenerPose{Math::Vec3(transform.world_matrix[3]),
                -Math::Vec3(transform.pose_world_matrix[2]),
                Math::Vec3(transform.pose_world_matrix[1])};
        });
        if(duplicate)
            return Result<void, Error>::failure({"Only one Audio Listener may be enabled"});
        if(auto configured = m_audio.set_listener(std::move(listener)); !configured)
            return configured;
        std::erase_if(m_entries, [this](const auto& item) {
            const auto& entry = item.second;
            bool removed = !entry.entity
                           || !entry.entity.template has_component<AudioSourceComponent>()
                           || !m_audio.has_voice(entry.voice);
            if(!removed) {
                const auto& source = entry.entity.template get_component<AudioSourceComponent>();
                removed = entry.entity.get_uuid() != item.first
                          || source.lifetime() != entry.lifetime || source.clip != entry.clip
                          || entry.revision != m_audio.clip_revision(entry.clip)
                          || !source.play_on_start;
            }
            if(removed)
                m_audio.remove_voice(entry.voice);
            return removed;
        });

        std::map<EntityUuid, Entity> pending;
        scene.each<const AudioSourceComponent>(
            [&](Entity entity, const AudioSourceComponent& source) {
                if(source.clip && source.play_on_start && !m_entries.contains(entity.get_uuid()))
                    pending.emplace(entity.get_uuid(), entity);
            });
        for(const auto& [uuid, entity] : pending) {
            const auto& source = entity.get_component<AudioSourceComponent>();
            auto voice =
                m_audio.start_voice(source.clip, source.volume, source.loop, source.category,
                    source.spatial_settings(Math::Vec3(scene.get_world_matrix(entity)[3])));
            if(!voice)
                return Result<void, Error>::failure(voice.error());
            if(voice.value())
                m_entries.emplace(uuid, Entry{entity, source.clip, source.lifetime(),
                                            m_audio.clip_revision(source.clip), voice.value()});
        }
        for(const auto& [uuid, entry] : m_entries) {
            const auto& source = entry.entity.get_component<AudioSourceComponent>();
            if(auto updated = m_audio.update_voice(entry.voice, source.volume, source.loop,
                   source.category,
                   source.spatial_settings(Math::Vec3(scene.get_world_matrix(entry.entity)[3])));
                !updated)
                return updated;
        }
        return m_audio.flush_requests();
    }

    void AudioSystem::on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept {
        m_entries.clear();
    }
}
