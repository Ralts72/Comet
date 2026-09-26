#include "scene/systems/audio_system.h"

#include "asset/registry.h"
#include "diagnostics/logger.h"
#include "scene/scene.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Comet {
    Result<void, Error> AudioSystem::on_start(Scene& scene) {
        if(m_scene)
            return Result<void, Error>::failure({"Audio system is already running"});
        m_scene = &scene;
        return synchronize(scene);
    }

    Result<void, Error> AudioSystem::update(Scene& scene, const Context&) {
        if(m_scene != &scene)
            return Result<void, Error>::failure({"Audio system requires its active scene"});
        return synchronize(scene);
    }

    Result<void, Error> AudioSystem::prepare_playback() {
        if(m_playback || m_device_unavailable)
            return Result<void, Error>::success();
        auto playback = AudioPlayback::create(m_mode);
        if(!playback) {
            if(m_mode == AudioPlayback::Mode::Offline)
                return Result<void, Error>::failure(playback.error());
            LOG_WARN("Audio output unavailable; continuing without sound: {}",
                playback.error().message);
            m_device_unavailable = true;
        } else {
            m_playback = std::move(playback).value();
        }
        return Result<void, Error>::success();
    }

    Result<void, Error> AudioSystem::synchronize(Scene& scene) {
        std::erase_if(m_one_shots,
            [](const auto& voice) { return !voice->is_playing(); });
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
                if(source.clip && source.play_on_start
                    && !m_entries.contains(entity.get_uuid()))
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

    void AudioSystem::on_stop(Scene&) noexcept {
        m_one_shots.clear();
        m_entries.clear();
        m_playback.reset();
        m_device_unavailable = false;
        m_scene = nullptr;
    }
}
