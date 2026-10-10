#include "audio/audio.h"
#include "common/scope_exit.h"

#define MA_NO_ENCODING
#define MA_NO_FLAC
#define MA_NO_MP3
#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace Comet {
    namespace {
        constexpr std::uint64_t MAX_DECODED_SAMPLES = 16'000'000;

        Error audio_error(std::string operation, const ma_result result) {
            return {std::move(operation) + ": " + ma_result_description(result)};
        }
    }

    Result<std::shared_ptr<AudioClip>, Error> AudioClip::load(const std::filesystem::path& path) {
        using Load = Result<std::shared_ptr<AudioClip>, Error>;
        ma_decoder decoder{};
        const auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
#ifdef _WIN32
        const auto source = path.wstring();
        const auto opened = ma_decoder_init_file_w(source.c_str(), &config, &decoder);
#else
        const auto source = path.string();
        const auto opened = ma_decoder_init_file(source.c_str(), &config, &decoder);
#endif
        if(opened != MA_SUCCESS)
            return Load::failure(
                audio_error("Cannot decode audio '" + path.string() + "'", opened));
        const ScopeExit close([&] { ma_decoder_uninit(&decoder); });
        ma_uint64 frames = 0;
        const auto length = ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
        if(length != MA_SUCCESS)
            return Load::failure(audio_error("Cannot read audio duration", length));
        const auto channels = decoder.outputChannels;
        const auto rate = decoder.outputSampleRate;
        if(frames == 0 || channels == 0 || channels > 2 || rate == 0
            || frames > MAX_DECODED_SAMPLES / channels)
            return Load::failure(
                {"Audio clip is empty, has unsupported channels, or is too large"});
        std::vector<float> samples(static_cast<std::size_t>(frames * channels));
        ma_uint64 read = 0;
        const auto decoded = ma_decoder_read_pcm_frames(&decoder, samples.data(), frames, &read);
        if(decoded != MA_SUCCESS && decoded != MA_AT_END)
            return Load::failure(audio_error("Cannot read audio samples", decoded));
        if(read != frames)
            return Load::failure({"Audio clip ended before its declared frame count"});
        return Load::success(
            std::shared_ptr<AudioClip>(new AudioClip(std::move(samples), channels, rate)));
    }

    struct AudioPlayback::Impl {
        ma_engine engine{};
        std::array<ma_sound_group, 2> groups{};
        std::size_t initialized_groups = 0;
        Mode mode = Mode::Realtime;
        double silent_frame_remainder = 0;
        bool paused = false;
        bool initialized = false;

        ~Impl() {
            while(initialized_groups > 0)
                ma_sound_group_uninit(&groups[--initialized_groups]);
            if(initialized)
                ma_engine_uninit(&engine);
        }
    };

    struct AudioPlayback::Voice::Impl {
        std::shared_ptr<AudioPlayback::Impl> playback;
        std::shared_ptr<const AudioClip> clip;
        ma_audio_buffer buffer{};
        ma_sound sound{};
        AudioCategory category = AudioCategory::Effects;
        bool buffer_initialized = false;
        bool sound_initialized = false;

        ~Impl() {
            if(sound_initialized)
                ma_sound_uninit(&sound);
            if(buffer_initialized)
                ma_audio_buffer_uninit(&buffer);
        }
    };

    AudioPlayback::AudioPlayback(std::shared_ptr<Impl> impl) : m_impl(std::move(impl)) {}
    AudioPlayback::~AudioPlayback() = default;

    Result<std::unique_ptr<AudioPlayback>, Error> AudioPlayback::create(
        const Mode mode, const bool start_paused) {
        using Creation = Result<std::unique_ptr<AudioPlayback>, Error>;
        auto impl = std::make_shared<Impl>();
        impl->mode = mode;
        impl->paused = start_paused;
        auto config = ma_engine_config_init();
        config.noAutoStart = start_paused ? MA_TRUE : MA_FALSE;
        if(mode == Mode::Offline) {
            config.noDevice = MA_TRUE;
            config.channels = 2;
            config.sampleRate = 48000;
        }
        const auto result = ma_engine_init(&config, &impl->engine);
        if(result != MA_SUCCESS)
            return Creation::failure(audio_error("Cannot initialize audio playback", result));
        impl->initialized = true;
        // 分类通道只调增益；关闭变调，避免额外重采样及换路时的采样延迟。
        for(auto& group : impl->groups) {
            const auto grouped = ma_sound_group_init(&impl->engine,
                MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, nullptr, &group);
            if(grouped != MA_SUCCESS)
                return Creation::failure(audio_error("Cannot create audio mix group", grouped));
            ++impl->initialized_groups;
        }
        return Creation::success(
            std::unique_ptr<AudioPlayback>(new AudioPlayback(std::move(impl))));
    }

    Result<std::unique_ptr<AudioPlayback::Voice>, Error> AudioPlayback::create_voice(
        std::shared_ptr<const AudioClip> clip, const float volume, const bool looping,
        const AudioCategory category) {
        using Creation = Result<std::unique_ptr<Voice>, Error>;
        if(!clip || !std::isfinite(volume) || volume < 0 || volume > 1
            || !valid_audio_category(category))
            return Creation::failure(
                {"Audio voice requires a clip, volume in [0, 1] and category"});
        auto impl = std::make_unique<Voice::Impl>();
        impl->playback = m_impl;
        impl->clip = std::move(clip);
        impl->category = category;
        auto buffer_config = ma_audio_buffer_config_init(ma_format_f32, impl->clip->channels(),
            impl->clip->frame_count(), impl->clip->samples().data(), nullptr);
        buffer_config.sampleRate = impl->clip->sample_rate();
        const auto buffered = ma_audio_buffer_init(&buffer_config, &impl->buffer);
        if(buffered != MA_SUCCESS)
            return Creation::failure(audio_error("Cannot create audio buffer", buffered));
        impl->buffer_initialized = true;
        const auto created = ma_sound_init_from_data_source(&m_impl->engine, &impl->buffer,
            MA_SOUND_FLAG_NO_SPATIALIZATION, &m_impl->groups[static_cast<std::size_t>(category)],
            &impl->sound);
        if(created != MA_SUCCESS)
            return Creation::failure(audio_error("Cannot create audio voice", created));
        impl->sound_initialized = true;
        ma_sound_set_volume(&impl->sound, volume);
        ma_sound_set_looping(&impl->sound, looping ? MA_TRUE : MA_FALSE);
        return Creation::success(std::unique_ptr<Voice>(new Voice(std::move(impl))));
    }

    Result<void, Error> AudioPlayback::apply_settings(const AudioSettings settings) {
        if(auto valid = settings.validate(); !valid)
            return Result<void, Error>::failure({valid.error()});
        const auto applied = ma_engine_set_volume(&m_impl->engine, settings.master_volume);
        if(applied != MA_SUCCESS)
            return Result<void, Error>::failure(audio_error("Cannot apply master volume", applied));
        ma_sound_group_set_volume(&m_impl->groups[static_cast<std::size_t>(AudioCategory::Effects)],
            settings.effects_volume);
        ma_sound_group_set_volume(
            &m_impl->groups[static_cast<std::size_t>(AudioCategory::Music)], settings.music_volume);
        return Result<void, Error>::success();
    }

    Result<void, Error> AudioPlayback::Voice::set_category(const AudioCategory category) {
        if(!valid_audio_category(category))
            return Result<void, Error>::failure({"Invalid audio category"});
        if(category == m_impl->category)
            return Result<void, Error>::success();
        const auto attached = ma_node_attach_output_bus(
            &m_impl->sound, 0, &m_impl->playback->groups[static_cast<std::size_t>(category)], 0);
        if(attached != MA_SUCCESS)
            return Result<void, Error>::failure(
                audio_error("Cannot change audio category", attached));
        m_impl->category = category;
        return Result<void, Error>::success();
    }

    Result<void, Error> AudioPlayback::set_paused(const bool paused) {
        if(m_impl->paused == paused)
            return Result<void, Error>::success();
        if(m_impl->mode == Mode::Realtime) {
            ma_result result;
            if(paused)
                result = ma_engine_stop(&m_impl->engine);
            else
                result = ma_engine_start(&m_impl->engine);
            if(result != MA_SUCCESS)
                return Result<void, Error>::failure(
                    audio_error("Cannot change audio state", result));
        }
        m_impl->paused = paused;
        return Result<void, Error>::success();
    }

    Result<void, Error> AudioPlayback::advance_silently(const double delta_time) {
        if(!m_impl->paused)
            return Result<void, Error>::failure({"Silent audio advance requires paused playback"});
        if(!std::isfinite(delta_time) || delta_time < 0 || delta_time > 1)
            return Result<void, Error>::failure({"Silent audio delta must be in [0, 1] seconds"});
        const double exact_frames = m_impl->silent_frame_remainder
                                    + delta_time * ma_engine_get_sample_rate(&m_impl->engine);
        const auto frames = static_cast<uint64_t>(std::floor(exact_frames + 1e-9));
        std::array<float, 4096> discarded;
        const auto capacity = discarded.size() / ma_engine_get_channels(&m_impl->engine);
        // 设备回调已停止；主线程独占混音推进，采样不送往设备。
        for(uint64_t remaining = frames; remaining > 0;) {
            const auto count = std::min<uint64_t>(remaining, capacity);
            const auto result =
                ma_engine_read_pcm_frames(&m_impl->engine, discarded.data(), count, nullptr);
            if(result != MA_SUCCESS)
                return Result<void, Error>::failure(audio_error("Cannot advance audio", result));
            remaining -= count;
        }
        m_impl->silent_frame_remainder = std::max(0.0, exact_frames - frames);
        return Result<void, Error>::success();
    }

    Result<void, Error> AudioPlayback::read_frames(const std::span<float> samples) {
        if(m_impl->mode != Mode::Offline)
            return Result<void, Error>::failure({"Only offline playback can read audio frames"});
        if(samples.size() % 2 != 0)
            return Result<void, Error>::failure({"Audio output requires complete stereo frames"});
        if(samples.empty())
            return Result<void, Error>::success();
        if(m_impl->paused) {
            std::ranges::fill(samples, 0.0f);
            return Result<void, Error>::success();
        }
        const auto result =
            ma_engine_read_pcm_frames(&m_impl->engine, samples.data(), samples.size() / 2, nullptr);
        if(result != MA_SUCCESS)
            return Result<void, Error>::failure(audio_error("Cannot read audio frames", result));
        return Result<void, Error>::success();
    }

    AudioPlayback::Voice::Voice(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
    AudioPlayback::Voice::~Voice() = default;

    Result<void, Error> AudioPlayback::Voice::start() {
        const auto result = ma_sound_start(&m_impl->sound);
        if(result != MA_SUCCESS)
            return Result<void, Error>::failure(audio_error("Cannot start audio voice", result));
        return Result<void, Error>::success();
    }

    void AudioPlayback::Voice::stop() noexcept {
        ma_sound_stop(&m_impl->sound);
    }

    void AudioPlayback::Voice::set_volume(const float volume) {
        ma_sound_set_volume(&m_impl->sound, volume);
    }

    void AudioPlayback::Voice::set_looping(const bool looping) {
        ma_sound_set_looping(&m_impl->sound, looping ? MA_TRUE : MA_FALSE);
    }

    bool AudioPlayback::Voice::is_playing() const {
        return ma_sound_is_playing(&m_impl->sound) == MA_TRUE;
    }
}
