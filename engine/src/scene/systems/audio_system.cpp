#include "scene/systems/audio_system.h"

#include "scene/scene.h"
#include "physics/physics_access.h"
#include <cmath>

#include <algorithm>
#include <optional>
#include <utility>

namespace Comet {
    namespace {
        EntityUuid acoustic_body(Scene& scene, Entity entity) {
            for(auto owner = entity; owner; owner = scene.get_parent(owner))
                if(owner.has_component<RigidBodyComponent>()
                    || owner.has_component<CharacterControllerComponent>())
                    return owner.get_uuid();
            return entity.get_uuid();
        }
    }
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

    Result<void, Error> AudioSystem::post_update(Scene& scene, const Context& context) {
        // 脚本与动画事件交付后再接纳声音，不提前消耗新请求的播放时长。
        return synchronize(scene, context.services.physics, context.delta_time);
    }

    Result<void, Error> AudioSystem::synchronize(
        Scene& scene, PhysicsAccess* physics, double delta_time) {
        scene.update_world_transforms();
        std::optional<AudioListenerPose> listener;
        EntityUuid listener_id;
        bool duplicate = false;
        scene.each<const AudioListenerComponent>([&](Entity entity, const auto& component) {
            if(!component.enabled)
                return;
            if(listener) {
                duplicate = true;
                return;
            }
            const auto& transform = entity.get_component<WorldTransformComponent>();
            listener_id = acoustic_body(scene, entity);
            listener = AudioListenerPose{Math::Vec3(transform.world_matrix[3]),
                -Math::Vec3(transform.pose_world_matrix[2]),
                Math::Vec3(transform.pose_world_matrix[1])};
        });
        if(duplicate)
            return Result<void, Error>::failure({"Only one Audio Listener may be enabled"});
        if(auto configured = m_audio.set_listener(listener); !configured)
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
                m_audio.remove_voice(entry.voice, entry.fade_out);
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
                    source.spatial_settings(Math::Vec3(scene.get_world_matrix(entity)[3])),
                    source.fade_in);
            if(!voice)
                return Result<void, Error>::failure(voice.error());
            if(voice.value())
                m_entries.emplace(uuid, Entry{entity, source.clip, source.lifetime(),
                                            m_audio.clip_revision(source.clip), voice.value()});
        }
        std::vector<Entry*> overdue;
        for(auto& [uuid, entry] : m_entries) {
            const auto& source = entry.entity.get_component<AudioSourceComponent>();
            entry.query_elapsed += delta_time;
            if(source.spatial && source.occlusion && physics && listener) {
                if(entry.query_elapsed >= .1)
                    overdue.push_back(&entry);
            } else
                entry.occlusion_target = 0;
        }
        // 优先服务等待最久的声源，避免固定 UUID 顺序使后面的声源长期饥饿。
        const auto count = std::min(overdue.size(), std::size_t(8));
        std::partial_sort(overdue.begin(), overdue.begin() + count, overdue.end(),
            [](const Entry* a, const Entry* b) {
                if(a->query_elapsed != b->query_elapsed)
                    return a->query_elapsed > b->query_elapsed;
                return a->entity.get_uuid() < b->entity.get_uuid();
            });
        for(std::size_t index = 0; index < count; ++index) {
            auto& entry = *overdue[index];
            const auto& source = entry.entity.get_component<AudioSourceComponent>();
            const auto position = Math::Vec3(scene.get_world_matrix(entry.entity)[3]);
            const auto ray = listener->position - position;
            entry.query_elapsed = 0;
            entry.occlusion_target = 0;
            if(Math::length(ray) <= .01f)
                continue;
            auto hit = physics->raycast(position, ray,
                {.ignored = acoustic_body(scene, entry.entity), .layers = source.occlusion_layers});
            if(!hit)
                return Result<void, Error>::failure({hit.error()});
            if(hit.value() && hit.value()->entity != listener_id)
                entry.occlusion_target = source.occlusion_strength;
        }
        for(auto& [uuid, entry] : m_entries) {
            const auto& source = entry.entity.get_component<AudioSourceComponent>();
            entry.fade_out = source.fade_out;
            const auto position = Math::Vec3(scene.get_world_matrix(entry.entity)[3]);
            entry.occlusion +=
                (entry.occlusion_target - entry.occlusion) * float(1 - std::exp(-delta_time * 12));
            if(auto updated = m_audio.update_voice(entry.voice, source.volume, source.loop,
                   source.category, source.spatial_settings(position),
                   {source.reverb_mix, source.reverb_decay, entry.occlusion});
                !updated)
                return updated;
        }
        return m_audio.flush_requests();
    }

    void AudioSystem::on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept {
        m_entries.clear();
    }
}
