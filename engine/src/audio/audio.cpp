#include "audio/audio.h"
#include "common/scope_exit.h"

#define MA_NO_ENCODING
#define MA_NO_FLAC
#define MA_NO_MP3
#include <miniaudio.h>

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
        bool initialized = false;

        ~Impl() {
            if(initialized)
                ma_engine_uninit(&engine);
        }
    };

    struct AudioPlayback::Voice::Impl {
        std::shared_ptr<AudioPlayback::Impl> playback;
        std::shared_ptr<const AudioClip> clip;
        ma_audio_buffer buffer{};
        ma_sound sound{};
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

    Result<std::unique_ptr<AudioPlayback>, Error> AudioPlayback::create(const Mode mode) {
        using Creation = Result<std::unique_ptr<AudioPlayback>, Error>;
        auto impl = std::make_shared<Impl>();
        auto config = ma_engine_config_init();
        if(mode == Mode::Offline) {
            config.noDevice = MA_TRUE;
            config.channels = 2;
            config.sampleRate = 48000;
        }
        const auto result = ma_engine_init(&config, &impl->engine);
        if(result != MA_SUCCESS)
            return Creation::failure(audio_error("Cannot initialize audio playback", result));
        impl->initialized = true;
        return Creation::success(
            std::unique_ptr<AudioPlayback>(new AudioPlayback(std::move(impl))));
    }

    Result<std::unique_ptr<AudioPlayback::Voice>, Error> AudioPlayback::create_voice(
        std::shared_ptr<const AudioClip> clip, const float volume, const bool looping) {
        using Creation = Result<std::unique_ptr<Voice>, Error>;
        if(!clip || !std::isfinite(volume) || volume < 0 || volume > 1)
            return Creation::failure({"Audio voice requires a clip and volume in [0, 1]"});
        auto impl = std::make_unique<Voice::Impl>();
        impl->playback = m_impl;
        impl->clip = std::move(clip);
        auto buffer_config = ma_audio_buffer_config_init(ma_format_f32, impl->clip->channels(),
            impl->clip->frame_count(), impl->clip->samples().data(), nullptr);
        buffer_config.sampleRate = impl->clip->sample_rate();
        const auto buffered = ma_audio_buffer_init(&buffer_config, &impl->buffer);
        if(buffered != MA_SUCCESS)
            return Creation::failure(audio_error("Cannot create audio buffer", buffered));
        impl->buffer_initialized = true;
        const auto created = ma_sound_init_from_data_source(
            &m_impl->engine, &impl->buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &impl->sound);
        if(created != MA_SUCCESS)
            return Creation::failure(audio_error("Cannot create audio voice", created));
        impl->sound_initialized = true;
        ma_sound_set_volume(&impl->sound, volume);
        ma_sound_set_looping(&impl->sound, looping ? MA_TRUE : MA_FALSE);
        return Creation::success(std::unique_ptr<Voice>(new Voice(std::move(impl))));
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
