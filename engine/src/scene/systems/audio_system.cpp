#include "scene/systems/audio_system.h"

#include "asset/registry.h"
#include "diagnostics/logger.h"
#include "scene/scene.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Comet {
    Result<void, Error> AudioSystem::on_start(Scene& scene, RuntimeSession&) {
        return synchronize(scene);
    }

    Result<void, Error> AudioSystem::update(Scene& scene, const Context& context) {
        if(m_paused && m_playback) {
            if(auto advanced = m_playback->advance_silently(context.delta_time); !advanced)
                return advanced;
        }
        // 当前更新产生的请求在步末生效，不能提前消耗它们的播放时长。
        return synchronize(scene);
    }

    void AudioSystem::on_pause_changed(const bool paused) noexcept {
        m_paused = paused;
        if(!m_playback)
            return;
        if(auto changed = m_playback->set_paused(paused); !changed) {
            LOG_WARN(
                "Audio state change failed; continuing without sound: {}", changed.error().message);
            m_one_shots.clear();
            m_entries.clear();
            m_playback.reset();
            m_device_unavailable = true;
        }
    }

    Result<void, Error> AudioSystem::prepare_playback() {
        if(m_playback || m_device_unavailable)
            return Result<void, Error>::success();
        auto playback = AudioPlayback::create(m_mode, m_paused);
        if(!playback) {
            if(m_mode == AudioPlayback::Mode::Offline)
                return Result<void, Error>::failure(playback.error());
            LOG_WARN(
                "Audio output unavailable; continuing without sound: {}", playback.error().message);
            m_device_unavailable = true;
        } else {
            m_playback = std::move(playback).value();
        }
        return Result<void, Error>::success();
    }

    Result<void, Error> AudioSystem::synchronize(Scene& scene) {
        std::erase_if(m_one_shots, [](const auto& voice) { return !voice->is_playing(); });
        std::erase_if(m_entries, [](const auto& item) {
            const auto& entry = item.second;
            if(!entry.entity || !entry.entity.template has_component<AudioSourceComponent>())
                return true;
            const auto& source = entry.entity.template get_component<AudioSourceComponent>();
            return entry.entity.get_uuid() != item.first || source.lifetime() != entry.lifetime
                   || source.clip != entry.clip || !source.play_on_start;
        });

        std::map<EntityUuid, Entity> pending;
        scene.each<const AudioSourceComponent>(
            [&](Entity entity, const AudioSourceComponent& source) {
                if(source.clip && source.play_on_start && !m_entries.contains(entity.get_uuid()))
                    pending.emplace(entity.get_uuid(), entity);
            });
        for(const auto& [uuid, entity] : pending) {
            const auto& source = entity.get_component<AudioSourceComponent>();
            if(!std::isfinite(source.volume) || source.volume < 0 || source.volume > 1)
                return Result<void, Error>::failure({"Audio source volume must be in [0, 1]"});
            auto clip = m_assets.resolve<AudioClip>(source.clip);
            if(!clip)
                return Result<void, Error>::failure(
                    {"Audio clip is unavailable: " + std::to_string(source.clip.value())});
            if(auto ready = prepare_playback(); !ready)
                return ready;
            if(m_device_unavailable)
                continue;
            auto voice = m_playback->create_voice(clip, source.volume, source.loop);
            if(!voice)
                return Result<void, Error>::failure(voice.error());
            if(auto started = voice.value()->start(); !started)
                return started;
            m_entries.emplace(
                uuid, Entry{entity, source.clip, source.lifetime(), std::move(voice).value()});
        }

        for(auto& [uuid, entry] : m_entries) {
            const auto& source = entry.entity.get_component<AudioSourceComponent>();
            if(!std::isfinite(source.volume) || source.volume < 0 || source.volume > 1)
                return Result<void, Error>::failure({"Audio source volume must be in [0, 1]"});
            entry.voice->set_volume(source.volume);
            entry.voice->set_looping(source.loop);
        }

        for(const auto& request : scene.take_audio_play_requests()) {
            // 先到先播；饱和请求直接丢弃，不能恢复后再补播过期音效。
            if(m_one_shots.size() >= MAX_ONE_SHOT_VOICES) {
                if(!m_one_shot_limit_reported) {
                    LOG_WARN("One-shot voice limit ({}) reached; dropping excess requests",
                        MAX_ONE_SHOT_VOICES);
                    m_one_shot_limit_reported = true;
                }
                continue;
            }
            auto clip = m_assets.resolve<AudioClip>(request.clip);
            if(!clip)
                return Result<void, Error>::failure(
                    {"Audio clip is unavailable: " + std::to_string(request.clip.value())});
            if(auto ready = prepare_playback(); !ready)
                return ready;
            if(m_device_unavailable)
                continue;
            auto voice = m_playback->create_voice(clip, request.volume, false);
            if(!voice)
                return Result<void, Error>::failure(voice.error());
            if(auto started = voice.value()->start(); !started)
                return started;
            m_one_shots.push_back(std::move(voice).value());
        }
        return Result<void, Error>::success();
    }

    void AudioSystem::on_stop(Scene&, RuntimeSession&) noexcept {
        m_one_shots.clear();
        m_entries.clear();
        m_playback.reset();
        m_device_unavailable = false;
        m_paused = false;
        m_one_shot_limit_reported = false;
    }
}
