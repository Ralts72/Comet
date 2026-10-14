#pragma once

#include "audio/audio.h"
#include "audio/audio_commands.h"

#include <functional>
#include <map>
#include <optional>

namespace Comet {
    class AssetRegistry;
    class AudioSystem;

    // 每个运行域独立拥有命令队列、设备及全部 Voice。
    class COMET_API AudioService final: public AudioCommands {
    public:
        using PlaybackFactory =
            std::function<Result<std::unique_ptr<AudioPlayback>, Error>(AudioPlayback::Mode, bool)>;

        explicit AudioService(const AssetRegistry& assets,
            AudioPlayback::Mode mode = AudioPlayback::Mode::Realtime,
            PlaybackFactory create_playback = AudioPlayback::create);
        ~AudioService() override;
        AudioService(const AudioService&) = delete;
        AudioService& operator=(const AudioService&) = delete;

        [[nodiscard]] bool is_bound_to(const Scene& scene) const noexcept override;
        [[nodiscard]] bool request_one_shot(AssetHandle clip, float volume,
            AudioCategory category = AudioCategory::Effects,
            AudioSpatialSettings spatial = {}) override;
        [[nodiscard]] Result<void, Error> apply_settings(AudioSettings settings);
        [[nodiscard]] const AudioSettings& settings() const { return m_settings; }
        // 离线输出用于混音验收；未创建输出时返回静音。
        [[nodiscard]] Result<void, Error> read_frames(std::span<float> samples);

    private:
        friend class AudioSystem;
        using VoiceId = std::uint64_t;
        static constexpr std::size_t MAX_REQUESTS = 128;
        static constexpr std::size_t MAX_ONE_SHOTS = 64;
        struct PlayRequest {
            AssetHandle clip;
            float volume;
            AudioCategory category;
            AudioSpatialSettings spatial;
        };

        bool begin(const Scene& scene, bool paused) override;
        void end() noexcept override;
        void set_paused(bool paused) noexcept override;
        [[nodiscard]] Result<void, Error> advance(double delta_time);
        [[nodiscard]] Result<void, Error> flush_requests();
        [[nodiscard]] Result<void, Error> prepare_playback();
        [[nodiscard]] Result<void, Error> set_listener(std::optional<AudioListenerPose> listener);
        [[nodiscard]] Result<std::unique_ptr<AudioPlayback::Voice>, Error> prepare_voice(
            AssetHandle clip, float volume, bool looping, AudioCategory category,
            const AudioSpatialSettings& spatial);
        [[nodiscard]] Result<VoiceId, Error> start_voice(AssetHandle clip, float volume,
            bool looping, AudioCategory category, const AudioSpatialSettings& spatial);
        [[nodiscard]] bool has_voice(VoiceId voice) const;
        [[nodiscard]] uint64_t clip_revision(AssetHandle clip) const;
        [[nodiscard]] Result<void, Error> update_voice(VoiceId voice, float volume, bool looping,
            AudioCategory category, const AudioSpatialSettings& spatial);
        void remove_voice(VoiceId voice);

        const AssetRegistry& m_assets;
        AudioPlayback::Mode m_mode;
        PlaybackFactory m_create_playback;
        AudioSettings m_settings;
        std::optional<AudioListenerPose> m_listener;
        const Scene* m_scene = nullptr;
        std::unique_ptr<AudioPlayback> m_playback;
        std::map<VoiceId, std::unique_ptr<AudioPlayback::Voice>> m_voices;
        std::vector<std::unique_ptr<AudioPlayback::Voice>> m_one_shots;
        std::vector<PlayRequest> m_requests;
        VoiceId m_next_voice = 1;
        bool m_device_unavailable = false;
        bool m_paused = false;
        bool m_one_shot_limit_reported = false;
    };
}
