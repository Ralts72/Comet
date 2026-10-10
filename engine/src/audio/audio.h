#pragma once

#include "audio/audio_settings.h"
#include "common/error.h"
#include "common/export.h"
#include "common/result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Comet {
    // 已解码、不可变的短音效；不持有播放设备或文件句柄。
    class COMET_API AudioClip final {
    public:
        [[nodiscard]] static Result<std::shared_ptr<AudioClip>, Error> load(
            const std::filesystem::path& path);

        [[nodiscard]] std::span<const float> samples() const noexcept { return m_samples; }
        [[nodiscard]] std::uint32_t channels() const noexcept { return m_channels; }
        [[nodiscard]] std::uint32_t sample_rate() const noexcept { return m_sample_rate; }
        [[nodiscard]] std::uint64_t frame_count() const noexcept {
            return m_samples.size() / m_channels;
        }

    private:
        AudioClip(std::vector<float> samples, std::uint32_t channels, std::uint32_t sample_rate)
            : m_samples(std::move(samples)), m_channels(channels), m_sample_rate(sample_rate) {}

        std::vector<float> m_samples;
        std::uint32_t m_channels;
        std::uint32_t m_sample_rate;
    };

    // 一份播放设备可创建多个独立声音实例；实例保活音频数据和设备。
    class COMET_API AudioPlayback final {
    public:
        enum class Mode { Realtime, Offline };
        class Voice;

        [[nodiscard]] static Result<std::unique_ptr<AudioPlayback>, Error> create(
            Mode mode = Mode::Realtime, bool start_paused = false);
        ~AudioPlayback();
        AudioPlayback(const AudioPlayback&) = delete;
        AudioPlayback& operator=(const AudioPlayback&) = delete;

        [[nodiscard]] Result<std::unique_ptr<Voice>, Error> create_voice(
            std::shared_ptr<const AudioClip> clip, float volume, bool looping,
            AudioCategory category = AudioCategory::Effects);
        [[nodiscard]] Result<void, Error> apply_settings(AudioSettings settings);
        // 暂停设备回调，保留各 Voice 的播放位置和启停状态。
        [[nodiscard]] Result<void, Error> set_paused(bool paused);
        // 仅暂停时：静默推进最多一秒，保留不足一个采样帧的时间余量。
        [[nodiscard]] Result<void, Error> advance_silently(double delta_time);
        // 仅 Offline：同步输出 48 kHz、双声道交错 float PCM，不访问音频设备。
        [[nodiscard]] Result<void, Error> read_frames(std::span<float> samples);

    private:
        struct Impl;
        explicit AudioPlayback(std::shared_ptr<Impl> impl);
        std::shared_ptr<Impl> m_impl;
    };

    class COMET_API AudioPlayback::Voice final {
    public:
        ~Voice();
        Voice(const Voice&) = delete;
        Voice& operator=(const Voice&) = delete;

        [[nodiscard]] Result<void, Error> start();
        void stop() noexcept;
        void set_volume(float volume);
        void set_looping(bool looping);
        [[nodiscard]] Result<void, Error> set_category(AudioCategory category);
        [[nodiscard]] bool is_playing() const;

    private:
        friend class AudioPlayback;
        struct Impl;
        explicit Voice(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> m_impl;
    };
}
