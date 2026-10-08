#include "audio/audio_service.h"

#include "asset/registry.h"
#include "common/scope_exit.h"
#include "diagnostics/logger.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Comet {
    AudioService::AudioService(const AssetRegistry& assets, const AudioPlayback::Mode mode,
        PlaybackFactory create_playback)
        : m_assets(assets), m_mode(mode), m_create_playback(std::move(create_playback)) {}

    AudioService::~AudioService() {
        end();
    }

    bool AudioService::is_bound_to(const Scene& scene) const noexcept {
        return m_scene == &scene;
    }

    bool AudioService::begin(const Scene& scene, const bool paused) {
        if(m_scene)
            return false;
        m_scene = &scene;
        m_paused = paused;
        return true;
    }

    void AudioService::end() noexcept {
        m_requests.clear();
        m_one_shots.clear();
        m_voices.clear();
        m_playback.reset();
        m_scene = nullptr;
        m_device_unavailable = false;
        m_paused = false;
        m_one_shot_limit_reported = false;
    }

    bool AudioService::request_one_shot(const AssetHandle clip, const float volume) {
        if(!m_scene || !clip || !std::isfinite(volume) || volume < 0 || volume > 1
            || m_requests.size() >= MAX_REQUESTS)
            return false;
        m_requests.push_back({clip, volume});
        return true;
    }

    Result<void, Error> AudioService::prepare_playback() {
        if(m_playback || m_device_unavailable)
            return Result<void, Error>::success();
        if(!m_create_playback)
            return Result<void, Error>::failure({"Audio playback factory is unavailable"});
        auto playback = m_create_playback(m_mode, m_paused);
        if(!playback) {
            if(m_mode == AudioPlayback::Mode::Offline)
                return Result<void, Error>::failure(playback.error());
            LOG_WARN(
                "Audio output unavailable; continuing without sound: {}", playback.error().message);
            m_device_unavailable = true;
        } else {
            m_playback = std::move(playback).value();
            if(!m_playback)
                return Result<void, Error>::failure({"Audio playback factory returned no output"});
        }
        return Result<void, Error>::success();
    }

    Result<std::unique_ptr<AudioPlayback::Voice>, Error> AudioService::prepare_voice(
        const AssetHandle handle, const float volume, const bool looping) {
        using Prepared = Result<std::unique_ptr<AudioPlayback::Voice>, Error>;
        if(!m_scene)
            return Prepared::failure({"Audio service is inactive"});
        if(!std::isfinite(volume) || volume < 0 || volume > 1)
            return Prepared::failure({"Audio source volume must be in [0, 1]"});
        auto clip = m_assets.resolve<AudioClip>(handle);
        if(!clip)
            return Prepared::failure(
                {"Audio clip is unavailable: " + std::to_string(handle.value())});
        if(auto ready = prepare_playback(); !ready)
            return Prepared::failure(ready.error());
        if(m_device_unavailable)
            return Prepared::success(nullptr);
        auto voice = m_playback->create_voice(std::move(clip), volume, looping);
        if(!voice)
            return voice;
        if(auto started = voice.value()->start(); !started)
            return Prepared::failure(started.error());
        return voice;
    }

    Result<AudioService::VoiceId, Error> AudioService::start_voice(
        const AssetHandle clip, const float volume, const bool looping) {
        using Started = Result<VoiceId, Error>;
        if(m_next_voice == std::numeric_limits<VoiceId>::max())
            return Started::failure({"Audio voice identifiers exhausted"});
        auto voice = prepare_voice(clip, volume, looping);
        if(!voice)
            return Started::failure(voice.error());
        if(!voice.value())
            return Started::success(0);
        const auto id = m_next_voice++;
        m_voices.emplace(id, std::move(voice).value());
        return Started::success(id);
    }

    bool AudioService::has_voice(const VoiceId voice) const {
        return m_voices.contains(voice);
    }

    Result<void, Error> AudioService::update_voice(
        const VoiceId voice, const float volume, const bool looping) {
        if(!std::isfinite(volume) || volume < 0 || volume > 1)
            return Result<void, Error>::failure({"Audio source volume must be in [0, 1]"});
        const auto found = m_voices.find(voice);
        if(found == m_voices.end())
            return Result<void, Error>::failure({"Audio voice is unavailable"});
        found->second->set_volume(volume);
        found->second->set_looping(looping);
        return Result<void, Error>::success();
    }

    void AudioService::remove_voice(const VoiceId voice) {
        m_voices.erase(voice);
    }

    void AudioService::set_paused(const bool paused) noexcept {
        m_paused = paused;
        if(!m_playback)
            return;
        if(auto changed = m_playback->set_paused(paused); !changed) {
            LOG_WARN(
                "Audio state change failed; continuing without sound: {}", changed.error().message);
            m_one_shots.clear();
            m_voices.clear();
            m_playback.reset();
            m_device_unavailable = true;
        }
    }

    Result<void, Error> AudioService::advance(const double delta_time) {
        if(m_paused && m_playback) {
            if(auto advanced = m_playback->advance_silently(delta_time); !advanced)
                return advanced;
        }
        std::erase_if(m_one_shots, [](const auto& voice) { return !voice->is_playing(); });
        return Result<void, Error>::success();
    }

    Result<void, Error> AudioService::flush_requests() {
        // 保留队列容量；失败和饱和都不留下延后补播的请求。
        const ScopeExit clear([this] { m_requests.clear(); });
        for(const auto& request : m_requests) {
            if(m_one_shots.size() >= MAX_ONE_SHOTS) {
                if(!m_one_shot_limit_reported) {
                    LOG_WARN("One-shot voice limit ({}) reached; dropping excess requests",
                        MAX_ONE_SHOTS);
                    m_one_shot_limit_reported = true;
                }
                continue;
            }
            auto voice = prepare_voice(request.clip, request.volume, false);
            if(!voice)
                return Result<void, Error>::failure(voice.error());
            if(voice.value())
                m_one_shots.push_back(std::move(voice).value());
        }
        return Result<void, Error>::success();
    }

    Result<void, Error> AudioService::read_frames(const std::span<float> samples) {
        if(m_mode != AudioPlayback::Mode::Offline)
            return Result<void, Error>::failure({"Only offline playback can read audio frames"});
        if(samples.size() % 2 != 0)
            return Result<void, Error>::failure({"Audio output requires complete stereo frames"});
        if(m_playback)
            return m_playback->read_frames(samples);
        std::ranges::fill(samples, 0.0f);
        return Result<void, Error>::success();
    }
}
