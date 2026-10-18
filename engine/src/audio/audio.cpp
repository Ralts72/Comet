#include "audio/audio.h"
#include "common/scope_exit.h"

#define MA_NO_ENCODING
#define MA_NO_FLAC
#define MA_NO_MP3
#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
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

    Result<std::shared_ptr<AudioClip>, Error> AudioClip::load(
        const std::filesystem::path& path, const bool stream) {
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
            || (!stream && frames > MAX_DECODED_SAMPLES / channels))
            return Load::failure(
                {"Audio clip is empty, has unsupported channels, or is too large"});
        if(stream) {
            auto clip = std::shared_ptr<AudioClip>(new AudioClip({}, channels, rate));
            clip->m_frames = frames;
            clip->m_source = std::filesystem::absolute(path);
            return Load::success(std::move(clip));
        }
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

    namespace {
        // 音频线程独占延迟线；Main 只发布原子参数，回调中不分配内存或访问 Scene。
        struct VoiceEffects {
            ma_node_base node{};
            struct Delay {
                std::vector<std::array<float, 2>> samples;
                size_t cursor = 0;
                std::array<float, 2> damped{};
            };
            std::array<Delay, 4> delays;
            std::array<float, 2> filtered{};
            std::atomic<float> wet{0}, decay{1.2f}, occlusion{0};
            std::atomic<uint32_t> tail{0};
            float sample_rate = 48000;
            bool active = false;

            static void process(ma_node* node, const float** input, ma_uint32* input_count,
                float** output, ma_uint32* output_count) {
                auto& effect = *reinterpret_cast<VoiceEffects*>(node);
                const auto count = *output_count;
                const float* source = input ? input[0] : nullptr;
                const auto available = source ? *input_count : 0;
                const float wet = effect.wet.load(std::memory_order_relaxed);
                const float occlusion = effect.occlusion.load(std::memory_order_relaxed);
                const bool active = wet > 0 || occlusion > 0;
                if(active && !effect.active) {
                    for(auto& delay : effect.delays) {
                        std::ranges::fill(delay.samples, std::array<float, 2>{});
                        delay.damped = {};
                    }
                    effect.filtered = {};
                }
                effect.active = active;
                if(!active) {
                    if(available)
                        std::copy_n(source, available * 2, output[0]);
                    std::fill(output[0] + available * 2, output[0] + count * 2, 0);
                    effect.tail.store(0, std::memory_order_relaxed);
                    *input_count = available;
                    return;
                }
                std::array<float, 4> feedback{};
                const float decay = effect.decay.load(std::memory_order_relaxed);
                for(size_t i = 0; i < feedback.size(); ++i)
                    feedback[i] = std::pow(
                        .001f, float(effect.delays[i].samples.size()) / effect.sample_rate / decay);
                const float cutoff = 18000 * std::pow(600.0f / 18000, occlusion);
                const float coefficient = 1 - std::exp(-2 * Math::PI * cutoff / effect.sample_rate);
                const float gain = 1 - occlusion * .8f;
                auto tail = effect.tail.load(std::memory_order_relaxed);
                for(ma_uint32 frame = 0; frame < count; ++frame) {
                    std::array<float, 2> dry{};
                    for(size_t channel = 0; channel < 2; ++channel) {
                        const float sample = frame < available ? source[frame * 2 + channel] : 0;
                        effect.filtered[channel] +=
                            coefficient * (sample - effect.filtered[channel]);
                        dry[channel] = occlusion > 0 ? effect.filtered[channel] * gain : sample;
                        output[0][frame * 2 + channel] = dry[channel];
                        if(std::abs(sample) > 1e-5f && wet > 0)
                            tail = uint32_t((decay * 2 + .1f) * effect.sample_rate);
                    }
                    if(wet > 0) {
                        std::array<float, 2> reverberated{};
                        for(size_t i = 0; i < effect.delays.size(); ++i) {
                            auto& delay = effect.delays[i];
                            auto& sample = delay.samples[delay.cursor];
                            for(size_t channel = 0; channel < 2; ++channel) {
                                delay.damped[channel] +=
                                    .35f * (sample[channel] - delay.damped[channel]);
                                reverberated[channel] += sample[channel] * .25f;
                                sample[channel] =
                                    dry[channel] + delay.damped[channel] * feedback[i];
                            }
                            delay.cursor = (delay.cursor + 1) % delay.samples.size();
                        }
                        for(size_t channel = 0; channel < 2; ++channel)
                            output[0][frame * 2 + channel] =
                                dry[channel] * (1 - wet * .5f) + reverberated[channel] * wet;
                    }
                    if(tail)
                        --tail;
                }
                effect.tail.store(wet > 0 ? tail : 0, std::memory_order_relaxed);
                *input_count = available;
            }
        };
        const ma_node_vtable voice_effects_vtable{VoiceEffects::process, nullptr, 1, 1,
            MA_NODE_FLAG_CONTINUOUS_PROCESSING | MA_NODE_FLAG_ALLOW_NULL_INPUT};
    }

    struct AudioPlayback::Impl {
        ma_engine engine{};
        ma_resource_manager resource_manager{};
        std::array<ma_sound_group, 2> groups{};
        std::size_t initialized_groups = 0;
        Mode mode = Mode::Realtime;
        double silent_frame_remainder = 0;
        bool paused = false;
        bool initialized = false;
        bool resource_manager_initialized = false;

        ma_result read_frames(float* samples, ma_uint64 frames) {
            if(!resource_manager_initialized)
                return ma_engine_read_pcm_frames(&engine, samples, frames, nullptr);
            // 离线时由调用者推进解码队列；小块混音让耗尽的页面及时补充。
            while(frames > 0) {
                ma_result job;
                while((job = ma_resource_manager_process_next_job(&resource_manager))
                      == MA_SUCCESS) {}
                if(job != MA_NO_DATA_AVAILABLE)
                    return job;
                const auto count = std::min<ma_uint64>(frames, 256);
                const auto result = ma_engine_read_pcm_frames(&engine, samples, count, nullptr);
                if(result != MA_SUCCESS)
                    return result;
                samples += count * ma_engine_get_channels(&engine);
                frames -= count;
            }
            return MA_SUCCESS;
        }

        ~Impl() {
            while(initialized_groups > 0)
                ma_sound_group_uninit(&groups[--initialized_groups]);
            if(initialized)
                ma_engine_uninit(&engine);
            if(resource_manager_initialized)
                ma_resource_manager_uninit(&resource_manager);
        }
    };

    struct AudioPlayback::Voice::Impl {
        std::shared_ptr<AudioPlayback::Impl> playback;
        std::shared_ptr<const AudioClip> clip;
        ma_audio_buffer buffer{};
        ma_sound sound{};
        VoiceEffects effects;
        bool effects_initialized = false;
        AudioCategory category = AudioCategory::Effects;
        bool buffer_initialized = false;
        bool sound_initialized = false;

        ~Impl() {
            if(sound_initialized)
                ma_sound_uninit(&sound);
            if(effects_initialized)
                ma_node_uninit(&effects.node, nullptr);
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
        config.channels = 2;
        config.sampleRate = 48000;
        if(mode == Mode::Offline) {
            config.noDevice = MA_TRUE;
            config.channels = 2;
            config.sampleRate = 48000;
            auto resources = ma_resource_manager_config_init();
            resources.flags = MA_RESOURCE_MANAGER_FLAG_NO_THREADING;
            resources.jobThreadCount = 0;
            resources.decodedFormat = ma_format_f32;
            resources.decodedSampleRate = 48000;
            const auto result = ma_resource_manager_init(&resources, &impl->resource_manager);
            if(result != MA_SUCCESS)
                return Creation::failure(
                    audio_error("Cannot initialize offline audio resources", result));
            impl->resource_manager_initialized = true;
            config.pResourceManager = &impl->resource_manager;
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
        const AudioCategory category, const AudioSpatialSettings spatial) {
        using Creation = Result<std::unique_ptr<Voice>, Error>;
        if(!clip || !std::isfinite(volume) || volume < 0 || volume > 1
            || !valid_audio_category(category))
            return Creation::failure(
                {"Audio voice requires a clip, volume in [0, 1] and category"});
        if(auto valid = spatial.validate(); !valid)
            return Creation::failure({valid.error()});
        auto impl = std::make_unique<Voice::Impl>();
        impl->playback = m_impl;
        impl->clip = std::move(clip);
        impl->category = category;
        auto* group = &m_impl->groups[static_cast<std::size_t>(category)];
        ma_result created;
        if(impl->clip->is_streamed()) {
            auto flags =
                MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_WAIT_INIT | MA_SOUND_FLAG_NO_SPATIALIZATION;
            // 在预读页面前设置循环，避免第一轮结束时才发起异步回卷。
            if(looping)
                flags |= MA_SOUND_FLAG_LOOPING;
#ifdef _WIN32
            created = ma_sound_init_from_file_w(&m_impl->engine,
                impl->clip->source_path().wstring().c_str(), flags, group, nullptr, &impl->sound);
#else
            created = ma_sound_init_from_file(&m_impl->engine,
                impl->clip->source_path().string().c_str(), flags, group, nullptr, &impl->sound);
#endif
        } else {
            auto buffer_config = ma_audio_buffer_config_init(ma_format_f32, impl->clip->channels(),
                impl->clip->frame_count(), impl->clip->samples().data(), nullptr);
            buffer_config.sampleRate = impl->clip->sample_rate();
            const auto buffered = ma_audio_buffer_init(&buffer_config, &impl->buffer);
            if(buffered != MA_SUCCESS)
                return Creation::failure(audio_error("Cannot create audio buffer", buffered));
            impl->buffer_initialized = true;
            created = ma_sound_init_from_data_source(&m_impl->engine, &impl->buffer,
                MA_SOUND_FLAG_NO_SPATIALIZATION, group, &impl->sound);
        }
        if(created != MA_SUCCESS)
            return Creation::failure(audio_error("Cannot create audio voice", created));
        impl->sound_initialized = true;
        impl->effects.sample_rate = float(ma_engine_get_sample_rate(&m_impl->engine));
        constexpr std::array delays{.0297f, .0371f, .0411f, .0437f};
        for(size_t i = 0; i < delays.size(); ++i)
            impl->effects.delays[i].samples.resize(size_t(delays[i] * impl->effects.sample_rate));
        const ma_uint32 channels = 2;
        auto effect_config = ma_node_config_init();
        effect_config.vtable = &voice_effects_vtable;
        effect_config.pInputChannels = &channels;
        effect_config.pOutputChannels = &channels;
        auto effect_result = ma_node_init(ma_engine_get_node_graph(&m_impl->engine), &effect_config,
            nullptr, &impl->effects.node);
        if(effect_result != MA_SUCCESS)
            return Creation::failure(audio_error("Cannot initialize voice effects", effect_result));
        impl->effects_initialized = true;
        effect_result = ma_node_attach_output_bus(&impl->effects.node, 0, group, 0);
        if(effect_result == MA_SUCCESS)
            effect_result = ma_node_attach_output_bus(&impl->sound, 0, &impl->effects.node, 0);
        if(effect_result != MA_SUCCESS)
            return Creation::failure(audio_error("Cannot connect voice effects", effect_result));
        ma_sound_set_volume(&impl->sound, volume);
        ma_sound_set_looping(&impl->sound, looping ? MA_TRUE : MA_FALSE);
        auto voice = std::unique_ptr<Voice>(new Voice(std::move(impl)));
        if(auto configured = voice->set_spatial(spatial); !configured)
            return Creation::failure(configured.error());
        return Creation::success(std::move(voice));
    }

    Result<void, Error> AudioPlayback::set_listener(const AudioListenerPose& listener) {
        const auto basis = Math::view_basis(listener.forward, listener.up);
        if(!Math::is_finite(listener.position) || !basis)
            return Result<void, Error>::failure(
                {"Audio listener requires a valid position and orientation"});
        const auto forward = -(*basis)[2];
        const auto up = (*basis)[1];
        const auto& position = listener.position;
        ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
        ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(&m_impl->engine, 0, up.x, up.y, up.z);
        return Result<void, Error>::success();
    }

    Result<void, Error> AudioPlayback::Voice::set_spatial(const AudioSpatialSettings& spatial) {
        if(auto valid = spatial.validate(); !valid)
            return Result<void, Error>::failure({valid.error()});
        ma_sound_set_spatialization_enabled(&m_impl->sound, spatial.enabled);
        ma_sound_set_attenuation_model(&m_impl->sound, ma_attenuation_model_linear);
        ma_sound_set_min_distance(&m_impl->sound, spatial.min_distance);
        ma_sound_set_max_distance(&m_impl->sound, spatial.max_distance);
        ma_sound_set_rolloff(&m_impl->sound, spatial.rolloff);
        ma_sound_set_position(
            &m_impl->sound, spatial.position.x, spatial.position.y, spatial.position.z);
        return Result<void, Error>::success();
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
        const auto attached = ma_node_attach_output_bus(&m_impl->effects.node, 0,
            &m_impl->playback->groups[static_cast<std::size_t>(category)], 0);
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
            const auto result = m_impl->read_frames(discarded.data(), count);
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
        const auto result = m_impl->read_frames(samples.data(), samples.size() / 2);
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

    Result<void, Error> AudioPlayback::Voice::set_effects(const AudioEffectSettings& effects) {
        if(auto valid = effects.validate(); !valid)
            return Result<void, Error>::failure({valid.error()});
        m_impl->effects.wet.store(effects.reverb_mix, std::memory_order_relaxed);
        m_impl->effects.decay.store(effects.reverb_decay, std::memory_order_relaxed);
        m_impl->effects.occlusion.store(effects.occlusion, std::memory_order_relaxed);
        return Result<void, Error>::success();
    }

    Result<void, Error> AudioPlayback::Voice::fade_in(float seconds) {
        if(!std::isfinite(seconds) || seconds < 0 || seconds > 60)
            return Result<void, Error>::failure({"Audio fade duration must be in [0, 60] seconds"});
        ma_sound_set_fade_in_milliseconds(
            &m_impl->sound, seconds > 0 ? 0 : 1, 1, ma_uint64(seconds * 1000));
        return Result<void, Error>::success();
    }

    Result<void, Error> AudioPlayback::Voice::fade_out(float seconds) {
        if(!std::isfinite(seconds) || seconds < 0 || seconds > 60)
            return Result<void, Error>::failure({"Audio fade duration must be in [0, 60] seconds"});
        const auto result =
            ma_sound_stop_with_fade_in_milliseconds(&m_impl->sound, ma_uint64(seconds * 1000));
        if(result != MA_SUCCESS)
            return Result<void, Error>::failure(audio_error("Cannot fade voice", result));
        return Result<void, Error>::success();
    }

    bool AudioPlayback::Voice::is_playing() const {
        return ma_sound_is_playing(&m_impl->sound) == MA_TRUE
               || m_impl->effects.tail.load(std::memory_order_relaxed) > 0;
    }
}
